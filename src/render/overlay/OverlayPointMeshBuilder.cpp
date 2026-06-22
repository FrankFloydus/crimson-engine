////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayPointMeshBuilder.h"

#include "core/ColorSpace.h"

#include <algorithm>
#include <cstdint>

namespace quader {

OverlayPointMesh OverlayPointMeshBuilder::build(
        const std::vector<const ViewportOverlayPoint*>& points,
        const ViewportOverlayCamera& camera,
        float sizePixels,
        float depthBias,
        std::uint32_t overrideColor,
        bool useOverrideColor) {
    OverlayPointMesh mesh;
    mesh.vertices.reserve(points.size() * 4u);
    mesh.indices.reserve(points.size() * 6u);
    const OverlayCameraProjector projector(camera);
    for (const ViewportOverlayPoint* point : points) {
        const std::uint32_t color = useOverrideColor ? overrideColor : point->color;
        (void)appendPointQuad(mesh, point->position, color, projector, sizePixels, depthBias);
    }
    return mesh;
}

bool OverlayPointMeshBuilder::appendPointQuad(
        OverlayPointMesh& mesh,
        Vec3 position,
        std::uint32_t color,
        const OverlayCameraProjector& projector,
        float sizePixels,
        float depthBias) {
    OverlayViewPoint center = projector.viewPointFor(position);
    projector.applyDepthBias(center, depthBias);
    if (projector.nearPlaneDistance(center) < 0.0f) {
        return false;
    }

    const OverlayProjectedPoint projected = projector.project(center);
    if (!projected.valid) {
        return false;
    }

    const float halfSize = std::max(sizePixels, 1.0f) * 0.5f;
    const std::uint32_t linearColor = rgba8SrgbToLinear(color);
    const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({projector.withPixelOffset(projected.device, {-halfSize, -halfSize}), {0.0f, 0.0f}, linearColor});
    mesh.vertices.push_back({projector.withPixelOffset(projected.device, {halfSize, -halfSize}), {1.0f, 0.0f}, linearColor});
    mesh.vertices.push_back({projector.withPixelOffset(projected.device, {halfSize, halfSize}), {1.0f, 1.0f}, linearColor});
    mesh.vertices.push_back({projector.withPixelOffset(projected.device, {-halfSize, halfSize}), {0.0f, 1.0f}, linearColor});
    mesh.indices.push_back(offset);
    mesh.indices.push_back(offset + 1u);
    mesh.indices.push_back(offset + 2u);
    mesh.indices.push_back(offset);
    mesh.indices.push_back(offset + 2u);
    mesh.indices.push_back(offset + 3u);
    return true;
}

} // namespace quader
