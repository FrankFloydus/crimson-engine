////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_builder.hpp"

#include "render/render_signature_builder.hpp"
#include "render/overlay/overlay_face_fill_builder.hpp"
#include "render/overlay/overlay_line_mesh_builder.hpp"
#include "render/overlay/overlay_point_mesh_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr float kOverlayVisibilityEpsilon = 0.000001f;

bool overlayLayerUsesDepthStampVisibility(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SourceWire ||
            layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
            layer == ViewportOverlayLayer::HoverFaceEdge ||
            layer == ViewportOverlayLayer::HoverEdge ||
            layer == ViewportOverlayLayer::SelectedVertex ||
            layer == ViewportOverlayLayer::HoverVertex;
}

QVec3 pointOnLine(QVec3 a, QVec3 b, float t) {
    return a + ((b - a) * t);
}

QVec3 cameraForward(const ViewportOverlayCamera& camera) {
    QVec3 forward = normalize(camera.forward);
    if (length_squared(forward) <= kOverlayVisibilityEpsilon) {
        forward = {0.0f, 0.0f, -1.0f};
    }
    return forward;
}

bool intersectRayTriangle(
        QVec3 origin,
        QVec3 direction,
        const ViewportOverlayTriangle& triangle,
        float& outDistance) {
    const QVec3 edge1 = triangle.b - triangle.a;
    const QVec3 edge2 = triangle.c - triangle.a;
    const QVec3 p = cross(direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= kOverlayVisibilityEpsilon) {
        return false;
    }
    const float invDeterminant = 1.0f / determinant;
    const QVec3 t = origin - triangle.a;
    const float u = dot(t, p) * invDeterminant;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const QVec3 q = cross(t, edge1);
    const float v = dot(direction, q) * invDeterminant;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float distance = dot(edge2, q) * invDeterminant;
    if (distance <= 0.0001f) {
        return false;
    }
    outDistance = distance;
    return true;
}

} // namespace

class OverlayDepthStampVisibilityFilter {
public:
    explicit OverlayDepthStampVisibilityFilter(const ViewportOverlayFrame& frame)
    {
        for (const ViewportOverlayTriangle& triangle : frame.triangles) {
            if (triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp &&
                    triangle.element.objectId >= 0) {
                stampsByObject_[triangle.element.objectId].push_back(&triangle);
            }
        }
    }

    [[nodiscard]] bool empty() const {
        return stampsByObject_.empty();
    }

    [[nodiscard]] bool lineVisible(const ViewportOverlayLine& line, const ViewportOverlayCamera& camera) const {
        const std::vector<const ViewportOverlayTriangle*>* stamps = stampsFor(line.element.objectId);
        if (stamps == nullptr) {
            return true;
        }
        return pointVisible(pointOnLine(line.start, line.end, 0.2f), camera, *stamps) &&
                pointVisible(pointOnLine(line.start, line.end, 0.5f), camera, *stamps) &&
                pointVisible(pointOnLine(line.start, line.end, 0.8f), camera, *stamps);
    }

    [[nodiscard]] bool pointVisible(const ViewportOverlayPoint& point, const ViewportOverlayCamera& camera) const {
        const std::vector<const ViewportOverlayTriangle*>* stamps = stampsFor(point.element.objectId);
        return stamps == nullptr || pointVisible(point.position, camera, *stamps);
    }

private:
    [[nodiscard]] const std::vector<const ViewportOverlayTriangle*>* stampsFor(int objectId) const {
        const auto found = stampsByObject_.find(objectId);
        return found == stampsByObject_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] static bool pointVisible(
            QVec3 point,
            const ViewportOverlayCamera& rawCamera,
            const std::vector<const ViewportOverlayTriangle*>& stamps) {
        const ViewportOverlayCamera camera = OverlayCameraProjector::sanitizedCamera(rawCamera);
        const QVec3 forward = cameraForward(camera);
        QVec3 origin;
        QVec3 direction;
        float targetDistance = 0.0f;
        if (camera.projection == ViewportOverlayProjection::Orthographic) {
            targetDistance = dot(point - camera.eye, forward);
            if (targetDistance <= 0.0001f) {
                return true;
            }
            origin = point - (forward * targetDistance);
            direction = forward;
        } else {
            const QVec3 toPoint = point - camera.eye;
            targetDistance = length(toPoint);
            if (targetDistance <= 0.0001f) {
                return true;
            }
            origin = camera.eye;
            direction = toPoint / targetDistance;
        }

        const float tolerance = std::max(0.001f, targetDistance * 0.00025f);
        const float maxDistance = targetDistance - tolerance;
        if (maxDistance <= 0.0001f) {
            return true;
        }
        for (const ViewportOverlayTriangle* triangle : stamps) {
            float hitDistance = 0.0f;
            if (intersectRayTriangle(origin, direction, *triangle, hitDistance) &&
                    hitDistance < maxDistance) {
                return false;
            }
        }
        return true;
    }

    std::unordered_map<int, std::vector<const ViewportOverlayTriangle*>> stampsByObject_;
};

std::uint64_t OverlayRenderableBuilder::frameSignature(const ViewportOverlayFrame& frame) {
    return RenderSignatureBuilder::overlayFrameSignature(frame);
}

std::uint64_t OverlayRenderableBuilder::cameraSignature(const ViewportOverlayCamera& camera) {
    return RenderSignatureBuilder::overlayCameraSignature(camera);
}

std::uint64_t OverlayRenderableBuilder::layerSignature(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer) {
    return RenderSignatureBuilder::overlayLayerSignature(frame, layer);
}

OverlayRenderState OverlayRenderableBuilder::makeState(
        ViewportOverlayLayer layer,
        ViewportOverlayGeometryKind kind,
        std::uint16_t blendOrder,
        bool depthStamp,
        bool faceColorPass) {
    const bool drawOnTop = viewportOverlayDepthMode(layer, kind) == ViewportOverlayDepthMode::DrawOnTop;
    OverlayRenderState state;
    state.depthWrite = depthStamp;
    state.colorWrite = !depthStamp;
    state.depthCulling = depthStamp || faceColorPass || !drawOnTop;
    state.depthFunc = depthStamp ? OverlayDepthFunc::GreaterOrEqual :
            faceColorPass ? OverlayDepthFunc::Equal :
            drawOnTop ? OverlayDepthFunc::Always :
            OverlayDepthFunc::GreaterOrEqual;
    const bool twoSidedTriangle = kind == ViewportOverlayGeometryKind::Triangles &&
            viewportOverlayTriangleIsTwoSided(layer);
    state.cullingMode = kind == ViewportOverlayGeometryKind::Triangles && !twoSidedTriangle
            ? OverlayCullingMode::Back
            : OverlayCullingMode::None;
    state.transparencyMode = kind == ViewportOverlayGeometryKind::Triangles && twoSidedTriangle
            ? OverlayTransparencyMode::TwoPassesTwoSides
            : kind == ViewportOverlayGeometryKind::Triangles
            ? OverlayTransparencyMode::TwoPassesOneSide
            : OverlayTransparencyMode::Default;
    if (kind == ViewportOverlayGeometryKind::Triangles) {
        state.polygonOffsetScale = -1.0f;
        state.polygonOffsetConstant = -1.0f;
    }
    state.blendOrder = blendOrder;
    return state;
}

OverlayLayerBuildPlan OverlayRenderableBuilder::buildLayer(
        const ViewportOverlayFrame& frame,
        const ViewportOverlayCamera& camera,
        ViewportOverlayLayer layer) {
    OverlayLayerBuildPlan plan;
    if (layer == ViewportOverlayLayer::SourceWireDepthStamp) {
        return plan;
    }

    std::uint16_t blendOrder = static_cast<std::uint16_t>(viewportOverlayDrawOrder(layer) * 64);
    OverlayFaceFillMesh faceFillMesh = OverlayFaceFillBuilder::buildLayer(frame, layer);
    if (!faceFillMesh.empty()) {
        if (viewportOverlayLayerUsesDepthOnlyFaceFill(layer)) {
            plan.faceFillBatches.push_back({
                    .mesh = std::move(faceFillMesh),
                    .state = makeState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, true, false),
            });
        } else if (viewportOverlayLayerUsesDepthStampedFaceFill(layer)) {
            plan.faceFillBatches.push_back({
                    .mesh = faceFillMesh,
                    .state = makeState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, true, false),
            });
            plan.faceFillBatches.push_back({
                    .mesh = std::move(faceFillMesh),
                    .state = makeState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, false, true),
            });
        } else {
            plan.faceFillBatches.push_back({
                    .mesh = std::move(faceFillMesh),
                    .state = makeState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++),
            });
        }
    }

    const OverlayDepthStampVisibilityFilter visibility(frame);
    const std::vector<const ViewportOverlayLine*> lines = linesForLayer(frame, layer, camera, visibility);
    const std::vector<LineBatchKey> lineKeys = lineBatchKeys(lines, layer);
    for (const LineBatchKey& key : lineKeys) {
        std::vector<const ViewportOverlayLine*> batchLines;
        batchLines.reserve(lines.size());
        for (const ViewportOverlayLine* line : lines) {
            if (line->color != key.color ||
                    viewportOverlayLineWidthPixels(layer, line->widthPixels) != key.widthPixels) {
                continue;
            }
            batchLines.push_back(line);
        }
        const float depthBias = viewportOverlayDepthBias(layer, ViewportOverlayGeometryKind::Lines);
        OverlayLineMesh mesh = OverlayLineMeshBuilder::build(batchLines, camera, key.widthPixels, depthBias);
        if (mesh.empty()) {
            continue;
        }
        plan.lineBatches.push_back({
                .mesh = std::move(mesh),
                .state = makeState(layer, ViewportOverlayGeometryKind::Lines, blendOrder++),
                .widthPixels = key.widthPixels,
                .depthBias = depthBias,
        });
    }

    const std::vector<const ViewportOverlayPoint*> points = pointsForLayer(frame, layer, camera, visibility);
    appendPointPass(plan, points, camera, layer, blendOrder, true);
    appendPointPass(plan, points, camera, layer, blendOrder, false);
    return plan;
}

std::vector<const ViewportOverlayLine*> OverlayRenderableBuilder::linesForLayer(
        const ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer,
        const ViewportOverlayCamera& camera,
        const OverlayDepthStampVisibilityFilter& visibility) {
    std::vector<const ViewportOverlayLine*> lines;
    lines.reserve(frame.lines.size());
    for (const ViewportOverlayLine& line : frame.lines) {
        if (line.layer == layer &&
                (!overlayLayerUsesDepthStampVisibility(layer) || visibility.lineVisible(line, camera))) {
            lines.push_back(&line);
        }
    }
    return lines;
}

std::vector<const ViewportOverlayPoint*> OverlayRenderableBuilder::pointsForLayer(
        const ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer,
        const ViewportOverlayCamera& camera,
        const OverlayDepthStampVisibilityFilter& visibility) {
    std::vector<const ViewportOverlayPoint*> points;
    points.reserve(frame.points.size());
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer == layer &&
                (!overlayLayerUsesDepthStampVisibility(layer) || visibility.pointVisible(point, camera))) {
            points.push_back(&point);
        }
    }
    return points;
}

std::vector<OverlayRenderableBuilder::LineBatchKey> OverlayRenderableBuilder::lineBatchKeys(
        const std::vector<const ViewportOverlayLine*>& lines,
        ViewportOverlayLayer layer) {
    std::vector<LineBatchKey> keys;
    for (const ViewportOverlayLine* line : lines) {
        const LineBatchKey key{
                .color = line->color,
                .widthPixels = viewportOverlayLineWidthPixels(layer, line->widthPixels),
        };
        const auto existing = std::find_if(keys.begin(), keys.end(), [&](const LineBatchKey& candidate) {
            return candidate.color == key.color && candidate.widthPixels == key.widthPixels;
        });
        if (existing == keys.end()) {
            keys.push_back(key);
        }
    }
    return keys;
}

void OverlayRenderableBuilder::appendPointPass(
        OverlayLayerBuildPlan& plan,
        const std::vector<const ViewportOverlayPoint*>& points,
        const ViewportOverlayCamera& camera,
        ViewportOverlayLayer layer,
        std::uint16_t& blendOrder,
        bool outline) {
    const auto outlineGrowthFor = [layer](const ViewportOverlayPoint* point) {
        return point->outlineGrowthPixels.value_or(viewportOverlayPointOutlineGrowthPixels(layer));
    };
    const bool anyOutline = std::any_of(points.begin(), points.end(), [&](const ViewportOverlayPoint* point) {
        return outlineGrowthFor(point) > 0.0f || viewportOverlayPointIsOutlineOnly(layer);
    });
    if (outline && !anyOutline) {
        return;
    }
    if (!outline && viewportOverlayPointIsOutlineOnly(layer)) {
        return;
    }

    std::vector<float> pointSizes;
    for (const ViewportOverlayPoint* point : points) {
        const float baseSize = viewportOverlayPointSizePixels(layer, point->sizePixels);
        const bool outlineUsesNativeColor = layer == ViewportOverlayLayer::SnapTargetVertex;
        const float sizePixels = outline && !outlineUsesNativeColor
                ? baseSize + outlineGrowthFor(point)
                : baseSize;
        if (std::find(pointSizes.begin(), pointSizes.end(), sizePixels) == pointSizes.end()) {
            pointSizes.push_back(sizePixels);
        }
    }

    const float depthBias = viewportOverlayDepthBias(layer, ViewportOverlayGeometryKind::Points);
    for (const float sizePixels : pointSizes) {
        std::vector<const ViewportOverlayPoint*> batchPoints;
        batchPoints.reserve(points.size());
        for (const ViewportOverlayPoint* point : points) {
            const float baseSize = viewportOverlayPointSizePixels(layer, point->sizePixels);
            const bool outlineUsesNativeColor = layer == ViewportOverlayLayer::SnapTargetVertex;
            const float pointSize = outline && !outlineUsesNativeColor
                    ? baseSize + outlineGrowthFor(point)
                    : baseSize;
            if (pointSize == sizePixels) {
                batchPoints.push_back(point);
            }
        }

        const bool overrideColor = outline && layer != ViewportOverlayLayer::SnapTargetVertex;
        OverlayPointMesh mesh = OverlayPointMeshBuilder::build(
                batchPoints,
                camera,
                sizePixels,
                depthBias,
                kOverlayVertexOutlineColor,
                overrideColor);
        if (!mesh.empty()) {
            plan.pointBatches.push_back({
                    .mesh = std::move(mesh),
                    .state = makeState(layer, ViewportOverlayGeometryKind::Points, blendOrder++),
                    .sizePixels = sizePixels,
                    .innerRadius = outline && viewportOverlayPointIsOutlineOnly(layer)
                            ? viewportOverlayPointInnerRadius(layer)
                            : 0.0f,
                    .depthBias = depthBias,
            });
        }
    }
}

} // namespace quader
