////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_box_overlay_builder.hpp"

#include "render/overlay/viewport_overlay_dash.hpp"
#include "render/overlay/viewport_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

/**
 * Stores a projected box preview endpoint for dash segmentation.
 */
struct OverlayScreenPoint {
    QVec2 position;
    float depth = 0.0f;
    bool visible = false;
};

ViewportOverlayLine boxPreviewLine(QVec3 start, QVec3 end) {
    return {
            .layer = ViewportOverlayLayer::ToolPreviewLine,
            .start = start,
            .end = end,
            .color = kOverlayToolBoxPreviewLineColor,
            .widthPixels = kOverlayToolBoxPreviewLineWidthPixels,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    };
}

void appendLine(ViewportOverlayFrame& overlayFrame, QVec3 start, QVec3 end) {
    overlayFrame.lines.push_back(boxPreviewLine(start, end));
}

OverlayScreenPoint projectOverlayWorldPoint(const ViewportOverlayCamera& camera, QVec3 worldPoint) {
    const float width = std::max(camera.viewportSize.x, 1.0f);
    const float height = std::max(camera.viewportSize.y, 1.0f);
    const float aspect = width / height;
    const QVec3 forward = viewportOverlayNormalizeOr(camera.forward, {0.0f, 0.0f, -1.0f});
    const QVec3 right = viewportOverlayNormalizeOr(camera.right, {1.0f, 0.0f, 0.0f});
    const QVec3 up = viewportOverlayNormalizeOr(camera.up, {0.0f, 1.0f, 0.0f});
    const QVec3 cameraDelta = worldPoint - camera.eye;
    const float depth = dot(cameraDelta, forward);
    if (depth <= camera.nearPlane || depth >= camera.farPlane) {
        return {};
    }

    float ndcX = 0.0f;
    float ndcY = 0.0f;
    if (camera.projection == ViewportOverlayProjection::Orthographic) {
        const float halfHeight = std::max(camera.orthographicSize, 0.0001f) * 0.5f;
        ndcX = dot(cameraDelta, right) / (halfHeight * aspect);
        ndcY = dot(cameraDelta, up) / halfHeight;
    } else {
        constexpr float kPi = 3.14159265358979323846f;
        const float fovRadians = std::clamp(camera.fovDegrees, 1.0f, 170.0f) * kPi / 180.0f;
        const float tanHalfFov = std::tan(fovRadians * 0.5f);
        ndcX = dot(cameraDelta, right) / (depth * tanHalfFov * aspect);
        ndcY = dot(cameraDelta, up) / (depth * tanHalfFov);
    }

    return {
            .position = {
                    (ndcX + 1.0f) * 0.5f * width,
                    (1.0f - ndcY) * 0.5f * height,
            },
            .depth = depth,
            .visible = std::isfinite(ndcX) && std::isfinite(ndcY),
    };
}

} // namespace

void ViewportBoxOverlayBuilder::append(
        ViewportOverlayFrame& overlayFrame,
        const ViewportOverlayCamera& camera,
        const ModelingBoxPreview& preview) {
    if (!preview.valid) {
        return;
    }

    if (preview.footprintCount < 4U) {
        return;
    }
    for (std::size_t index = 0; index < preview.footprintCount; ++index) {
        const QVec3 worldStart = preview.footprint[index];
        const QVec3 worldEnd = preview.footprint[(index + 1U) % preview.footprintCount];
        const OverlayScreenPoint screenStart = projectOverlayWorldPoint(camera, worldStart);
        const OverlayScreenPoint screenEnd = projectOverlayWorldPoint(camera, worldEnd);
        if (!screenStart.visible || !screenEnd.visible) {
            continue;
        }
        const QVec3 worldDelta = worldEnd - worldStart;
        appendViewportOverlayDashedEdge(
                screenStart.position,
                screenEnd.position,
                true,
                [&](QVec2, QVec2, float startT, float endT) {
                    appendLine(
                            overlayFrame,
                            worldStart + worldDelta * startT,
                            worldStart + worldDelta * endT);
                });
    }
}

} // namespace quader
