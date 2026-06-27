////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayLineMeshBuilder.h"

#include "core/ColorSpace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace quader {

OverlayLineMesh OverlayLineMeshBuilder::build(
        const std::vector<const ViewportOverlayLine*>& lines,
        const ViewportOverlayCamera& camera,
        float widthPixels,
        float depthBias) {
    OverlayLineMesh mesh;
    mesh.vertices.reserve(lines.size() * 4u);
    mesh.indices.reserve(lines.size() * 6u);
    const OverlayCameraProjector projector(camera);
    for (const ViewportOverlayLine* line : lines) {
        (void)appendLineQuad(mesh, *line, projector, widthPixels, depthBias);
    }
    return mesh;
}

bool OverlayLineMeshBuilder::appendLineQuad(
        OverlayLineMesh& mesh,
        const ViewportOverlayLine& line,
        const OverlayCameraProjector& projector,
        float widthPixels,
        float depthBias) {
    const std::optional<OverlayClippedLine> clipped =
            projector.clipLineToNearPlane(line.start, line.end);
    if (!clipped.has_value()) {
        return false;
    }

    OverlayProjectedPoint projectedA = projector.project(clipped->start);
    OverlayProjectedPoint projectedB = projector.project(clipped->end);
    if (!projectedA.valid || !projectedB.valid) {
        return false;
    }
    projector.applyDeviceDepthBias(projectedA, depthBias);
    projector.applyDeviceDepthBias(projectedB, depthBias);

    QVec2 pixelDirection = projectedB.pixel - projectedA.pixel;
    const float pixelLength = std::sqrt(pixelDirection.x * pixelDirection.x + pixelDirection.y * pixelDirection.y);
    if (pixelLength > 0.000001f) {
        pixelDirection = {pixelDirection.x / pixelLength, pixelDirection.y / pixelLength};
    } else {
        pixelDirection = {1.0f, 0.0f};
    }
    const QVec2 pixelNormal{-pixelDirection.y, pixelDirection.x};
    const float sideDistance = (std::max(widthPixels, 1.0f) + kOverlayLineFeatherPixels) * 0.5f;
    const QVec2 startCapOffset = pixelDirection * -sideDistance;
    const QVec2 endCapOffset = pixelDirection * sideDistance;
    const std::uint32_t linearColor = rgba8SrgbToLinear(line.color);
    const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());

    mesh.vertices.push_back({
            projector.withPixelOffset(projectedA.device, startCapOffset + pixelNormal * -sideDistance),
            {-sideDistance, 0.0f, 0.0f, 0.0f},
            linearColor,
    });
    mesh.vertices.push_back({
            projector.withPixelOffset(projectedB.device, endCapOffset + pixelNormal * -sideDistance),
            {-sideDistance, 0.0f, 0.0f, 0.0f},
            linearColor,
    });
    mesh.vertices.push_back({
            projector.withPixelOffset(projectedB.device, endCapOffset + pixelNormal * sideDistance),
            {sideDistance, 0.0f, 0.0f, 0.0f},
            linearColor,
    });
    mesh.vertices.push_back({
            projector.withPixelOffset(projectedA.device, startCapOffset + pixelNormal * sideDistance),
            {sideDistance, 0.0f, 0.0f, 0.0f},
            linearColor,
    });
    mesh.indices.push_back(offset);
    mesh.indices.push_back(offset + 1u);
    mesh.indices.push_back(offset + 2u);
    mesh.indices.push_back(offset);
    mesh.indices.push_back(offset + 2u);
    mesh.indices.push_back(offset + 3u);
    return true;
}

} // namespace quader
