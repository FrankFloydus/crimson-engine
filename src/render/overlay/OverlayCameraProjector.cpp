////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayCameraProjector.h"

#include <algorithm>
#include <cmath>

namespace quader {

OverlayCameraProjector::OverlayCameraProjector(ViewportOverlayCamera camera)
    : camera_(sanitizedCamera(camera)) {}

const ViewportOverlayCamera& OverlayCameraProjector::camera() const {
    return camera_;
}

ViewportOverlayCamera OverlayCameraProjector::sanitizedCamera(ViewportOverlayCamera camera) {
    camera.forward = normalize(camera.forward);
    if (length(camera.forward) <= 0.000001f) {
        camera.forward = {0.0f, 0.0f, -1.0f};
    }
    camera.right = normalize(camera.right);
    if (length(camera.right) <= 0.000001f || std::abs(dot(camera.right, camera.forward)) > 0.999f) {
        const Vec3 reference = std::abs(camera.forward.y) < 0.999f
                ? Vec3{0.0f, 1.0f, 0.0f}
                : Vec3{1.0f, 0.0f, 0.0f};
        camera.right = normalize(cross(camera.forward, reference));
    }
    camera.up = normalize(cross(camera.right, camera.forward));
    if (length(camera.up) <= 0.000001f) {
        camera.up = {0.0f, 1.0f, 0.0f};
    }
    camera.viewportSize.x = std::max(camera.viewportSize.x, 1.0f);
    camera.viewportSize.y = std::max(camera.viewportSize.y, 1.0f);
    camera.orthographicSize = std::max(camera.orthographicSize, 0.0001f);
    camera.fovDegrees = std::clamp(camera.fovDegrees, 1.0f, 170.0f);
    camera.nearPlane = std::max(camera.nearPlane, 0.0001f);
    camera.farPlane = std::max(camera.farPlane, camera.nearPlane + 0.001f);
    return camera;
}

OverlayViewPoint OverlayCameraProjector::viewPointFor(Vec3 world) const {
    const Vec3 relative = world - camera_.eye;
    const float depth = dot(relative, camera_.forward);
    return {
            .x = dot(relative, camera_.right),
            .y = dot(relative, camera_.up),
            .z = -depth,
    };
}

void OverlayCameraProjector::applyDepthBias(OverlayViewPoint& point, float depthBias) const {
    if (depthBias > 0.0f && point.z < -0.0001f) {
        point.z += std::min(depthBias, -point.z * 0.5f);
    }
}

float OverlayCameraProjector::nearPlaneDistance(const OverlayViewPoint& point) const {
    return -point.z - camera_.nearPlane;
}

std::optional<OverlayClippedLine> OverlayCameraProjector::clipLineToNearPlane(
        Vec3 start,
        Vec3 end,
        float depthBias) const {
    OverlayViewPoint a = viewPointFor(start);
    OverlayViewPoint b = viewPointFor(end);
    applyDepthBias(a, depthBias);
    applyDepthBias(b, depthBias);

    const float aNear = nearPlaneDistance(a);
    const float bNear = nearPlaneDistance(b);
    if (aNear < 0.0f && bNear < 0.0f) {
        return std::nullopt;
    }
    if (aNear < 0.0f || bNear < 0.0f) {
        const float t = std::clamp(aNear / (aNear - bNear), 0.0f, 1.0f);
        const OverlayViewPoint clipped{
                .x = a.x + (b.x - a.x) * t,
                .y = a.y + (b.y - a.y) * t,
                .z = a.z + (b.z - a.z) * t,
        };
        if (aNear < 0.0f) {
            a = clipped;
        } else {
            b = clipped;
        }
    }
    return OverlayClippedLine{.start = a, .end = b};
}

OverlayProjectedPoint OverlayCameraProjector::project(OverlayViewPoint point) const {
    OverlayProjectedPoint projected;

    if (camera_.projection == ViewportOverlayProjection::Perspective) {
        const float depth = -point.z;
        if (depth <= 0.000001f) {
            return projected;
        }
        const float aspect = camera_.viewportSize.x / camera_.viewportSize.y;
        const float tanHalfFov = std::tan((camera_.fovDegrees * kPi / 180.0f) * 0.5f);
        projected.device = {
                point.x / (depth * tanHalfFov * aspect),
                point.y / (depth * tanHalfFov),
                1.0f - (2.0f * camera_.nearPlane / depth),
                1.0f,
        };
    } else {
        const float aspect = camera_.viewportSize.x / camera_.viewportSize.y;
        const float halfHeight = camera_.orthographicSize * 0.5f;
        const float halfWidth = halfHeight * aspect;
        projected.device = {
                point.x / halfWidth,
                point.y / halfHeight,
                (-2.0f / (camera_.farPlane - camera_.nearPlane)) * point.z -
                        ((camera_.farPlane + camera_.nearPlane) / (camera_.farPlane - camera_.nearPlane)),
                1.0f,
        };
    }

    const Vec2 ndc{projected.device.x, projected.device.y};
    projected.pixel = {
            (ndc.x * 0.5f + 0.5f) * camera_.viewportSize.x,
            (ndc.y * 0.5f + 0.5f) * camera_.viewportSize.y,
    };
    projected.valid = true;
    return projected;
}

OverlayFloat4 OverlayCameraProjector::withPixelOffset(OverlayFloat4 device, Vec2 pixelOffset) const {
    device.x += pixelOffset.x * 2.0f / camera_.viewportSize.x;
    device.y += pixelOffset.y * 2.0f / camera_.viewportSize.y;
    return device;
}

} // namespace quader
