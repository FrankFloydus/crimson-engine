////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayRenderableBuilder.h"

#include "render/RenderSignatureBuilder.h"
#include "render/overlay/OverlayFaceFillBuilder.h"
#include "render/overlay/OverlayLineMeshBuilder.h"
#include "render/overlay/OverlayPointMeshBuilder.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace quader {

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

    const std::vector<const ViewportOverlayLine*> lines = linesForLayer(frame, layer);
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

    const std::vector<const ViewportOverlayPoint*> points = pointsForLayer(frame, layer);
    appendPointPass(plan, points, camera, layer, blendOrder, true);
    appendPointPass(plan, points, camera, layer, blendOrder, false);
    return plan;
}

std::vector<const ViewportOverlayLine*> OverlayRenderableBuilder::linesForLayer(
        const ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer) {
    std::vector<const ViewportOverlayLine*> lines;
    lines.reserve(frame.lines.size());
    for (const ViewportOverlayLine& line : frame.lines) {
        if (line.layer == layer) {
            lines.push_back(&line);
        }
    }
    return lines;
}

std::vector<const ViewportOverlayPoint*> OverlayRenderableBuilder::pointsForLayer(
        const ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer) {
    std::vector<const ViewportOverlayPoint*> points;
    points.reserve(frame.points.size());
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer == layer) {
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
