////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/ViewportSelectionMarqueeOverlayBuilder.h"

#include "editor/viewport/ViewportSelectionInteraction.h"
#include "render/ViewportOverlayPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace quader {
namespace {

QVec3 normalizedOr(QVec3 value, QVec3 fallback) {
    const float magnitude = length(value);
    return magnitude <= 0.000001f ? fallback : value / magnitude;
}

QVec3 marqueeWorldPoint(const ViewportOverlayCamera& camera, QVec2 screen) {
    const QVec3 forward = normalizedOr(camera.forward, {0.0f, 0.0f, -1.0f});
    QVec3 right = normalizedOr(camera.right, {1.0f, 0.0f, 0.0f});
    if (std::abs(dot(right, forward)) > 0.999f) {
        right = normalizedOr(cross(forward, {0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
    }
    const QVec3 up = normalizedOr(cross(right, forward), {0.0f, 1.0f, 0.0f});
    const float width = std::max(camera.viewportSize.x, 1.0f);
    const float height = std::max(camera.viewportSize.y, 1.0f);
    const float aspect = width / height;
    const float ndcX = (screen.x / width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (screen.y / height) * 2.0f;
    const float depth = std::max(camera.nearPlane + 0.05f, 0.1f);

    if (camera.projection == ViewportOverlayProjection::Orthographic) {
        const float halfHeight = std::max(camera.orthographicSize, 0.0001f) * 0.5f;
        return camera.eye +
                forward * depth +
                right * (ndcX * halfHeight * aspect) +
                up * (ndcY * halfHeight);
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float fovRadians = std::clamp(camera.fovDegrees, 1.0f, 170.0f) * kPi / 180.0f;
    const float tanHalfFov = std::tan(fovRadians * 0.5f);
    return camera.eye +
            forward * depth +
            right * (ndcX * depth * tanHalfFov * aspect) +
            up * (ndcY * depth * tanHalfFov);
}

void appendSegment(
        ViewportOverlayFrame& frame,
        const ViewportOverlayCamera& camera,
        QVec2 start,
        QVec2 end,
        std::uint32_t color) {
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::ToolPreviewLine,
            .start = marqueeWorldPoint(camera, start),
            .end = marqueeWorldPoint(camera, end),
            .color = color,
            .widthPixels = 1.5f,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    });
}

void appendEdge(
        ViewportOverlayFrame& frame,
        const ViewportOverlayCamera& camera,
        QVec2 start,
        QVec2 end,
        std::uint32_t color,
        bool dashed) {
    const QVec2 delta = end - start;
    const float edgeLength = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (edgeLength <= 0.001f) {
        return;
    }
    if (!dashed) {
        appendSegment(frame, camera, start, end, color);
        return;
    }

    constexpr float kDashPixels = 8.0f;
    constexpr float kGapPixels = 5.0f;
    for (float offset = 0.0f; offset < edgeLength; offset += kDashPixels + kGapPixels) {
        const float segmentEnd = std::min(offset + kDashPixels, edgeLength);
        appendSegment(
                frame,
                camera,
                start + delta * (offset / edgeLength),
                start + delta * (segmentEnd / edgeLength),
                color);
    }
}

} // namespace

void ViewportSelectionMarqueeOverlayBuilder::append(
        ViewportOverlayFrame& overlayFrame,
        const ViewportOverlayCamera& camera,
        const ViewportSelectionMarquee& marquee) {
    if (!marquee.active) {
        return;
    }

    const bool dashed = marquee.rule == ModelingBoxSelectionRule::Crossing;
    const QVec2 min{
            std::min(marquee.start.x, marquee.end.x),
            std::min(marquee.start.y, marquee.end.y),
    };
    const QVec2 max{
            std::max(marquee.start.x, marquee.end.x),
            std::max(marquee.start.y, marquee.end.y),
    };
    const std::array<QVec2, 4> corners{{
            {min.x, min.y},
            {max.x, min.y},
            {max.x, max.y},
            {min.x, max.y},
    }};
    const std::array<QVec3, 4> worldCorners{{
            marqueeWorldPoint(camera, corners[0]),
            marqueeWorldPoint(camera, corners[1]),
            marqueeWorldPoint(camera, corners[2]),
            marqueeWorldPoint(camera, corners[3]),
    }};
    overlayFrame.triangles.push_back({
            .layer = ViewportOverlayLayer::ToolPreviewFill,
            .a = worldCorners[0],
            .b = worldCorners[1],
            .c = worldCorners[2],
            .color = kOverlaySelectionMarqueeFillColor,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    });
    overlayFrame.triangles.push_back({
            .layer = ViewportOverlayLayer::ToolPreviewFill,
            .a = worldCorners[0],
            .b = worldCorners[2],
            .c = worldCorners[3],
            .color = kOverlaySelectionMarqueeFillColor,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    });
    for (std::size_t index = 0; index < corners.size(); ++index) {
        appendEdge(
                overlayFrame,
                camera,
                corners[index],
                corners[(index + 1U) % corners.size()],
                kOverlaySelectionMarqueeLineColor,
                dashed);
    }
}

} // namespace quader
