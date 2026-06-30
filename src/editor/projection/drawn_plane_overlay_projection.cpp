////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/drawn_plane_overlay_projection.hpp"

#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

/**
 * Stores the Drawn Plane Preview Frame data contract used by projection.
 */
struct DrawnPlanePreviewFrame {
    QVec3 lineStart;
    QVec3 lineEnd;
    QVec3 normal;
    QVec3 side;
    float lineLength = 0.0f;
};

bool buildDrawnPlanePreviewFrame(QVec3 point0, QVec3 point1, QVec3 support, DrawnPlanePreviewFrame& frame) {
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

float lengthSquared(QVec3 value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

} // namespace

QVec3 DrawnPlaneOverlayProjection::supportPoint(std::span<const QVec3> visiblePoints, QVec3 faceNormal) {
    if (visiblePoints.size() > 2U) {
        return visiblePoints[2];
    }
    if (visiblePoints.size() > 1U && lengthSquared(faceNormal) > 0.000001f) {
        return visiblePoints[0] + faceNormal;
    }
    return {};
}

void DrawnPlaneOverlayProjection::append(
        ViewportOverlayFrame& frame,
        std::span<const QVec3> visiblePoints,
        QVec3 support,
        bool hasLine) {
    std::array<QVec3, 3> points{};
    const std::size_t count = std::min<std::size_t>(visiblePoints.size(), points.size());
    for (std::size_t index = 0; index < count; ++index) {
        points[index] = visiblePoints[index];
    }
    if (count <= 0U) {
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        frame.points.push_back({
                .layer = ViewportOverlayLayer::ToolHandle,
                .position = points[index],
                .color = kOverlayToolHandleColor,
                .sizePixels = kOverlayDrawnPlanePointSizePixels,
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
            .widthPixels = kOverlayDrawnPlaneLineWidthPixels,
            .styleRole = ViewportOverlayStyleRole::ToolHandle,
            .source = ViewportOverlaySource::Tool,
    });

    DrawnPlanePreviewFrame planeFrame;
    if (!buildDrawnPlanePreviewFrame(points[0], points[1], support, planeFrame)) {
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

} // namespace quader
