////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/ViewportTransformGizmoOverlayBuilder.h"

#include "render/ViewportOverlayPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <editor/interactions/transform_gizmo.h>

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCircleTau = kPi * 2.0f;
constexpr float kMoveCenterRadiusPixels = 3.5f;
constexpr float kScaleHandleSizePixels = 13.0f;
constexpr float kScaleCenterSizePixels = 14.0f;
constexpr float kOldGizmoBaseSizePixels = 68.0f;
constexpr int kConeSegments = 14;
constexpr int kCenterCircleSegments = 18;

std::uint8_t channel(std::uint32_t color, int shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xffu);
}

std::uint8_t scaledByte(std::uint8_t value, float scale) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<float>(value) * scale), 0l, 255l));
}

std::uint32_t withAlpha(std::uint32_t color, float alpha) {
    const std::uint32_t clampedAlpha =
            static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (color & 0x00ffffffu) | (clampedAlpha << 24u);
}

std::uint32_t multiplyColor(std::uint32_t color, float rgbScale, float alphaScale = 1.0f) {
    return rgba8(
            scaledByte(channel(color, 0), rgbScale),
            scaledByte(channel(color, 8), rgbScale),
            scaledByte(channel(color, 16), rgbScale),
            scaledByte(channel(color, 24), alphaScale));
}

std::uint32_t colorForAxis(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return kOverlayToolAxisXColor;
    case quader_editor_core::TransformAxis::y:
        return kOverlayToolAxisYColor;
    case quader_editor_core::TransformAxis::z:
        return kOverlayToolAxisZColor;
    case quader_editor_core::TransformAxis::xy:
    case quader_editor_core::TransformAxis::xz:
    case quader_editor_core::TransformAxis::yz:
    case quader_editor_core::TransformAxis::all:
    case quader_editor_core::TransformAxis::none:
        return kOverlayToolAxisMixedColor;
    }
    return kOverlayToolAxisMixedColor;
}

std::uint32_t colorForPlane(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::xy:
        return kOverlayToolPlaneXYColor;
    case quader_editor_core::TransformAxis::yz:
        return kOverlayToolPlaneYZColor;
    case quader_editor_core::TransformAxis::xz:
        return kOverlayToolPlaneXZColor;
    default:
        return rgba8(255, 255, 255, 255);
    }
}

bool axisMatches(quader_editor_core::TransformAxis expected, quader_editor_core::TransformAxis actual) {
    return expected != quader_editor_core::TransformAxis::none && expected == actual;
}

bool highlighted(
        const quader_editor_core::TransformGizmoFrame& frame,
        quader_editor_core::TransformAxis axis) {
    return axisMatches(frame.info.active_axis, axis) || axisMatches(frame.info.hover_axis, axis);
}

std::uint32_t authoredAxisColor(
        const quader_editor_core::TransformGizmoFrame& frame,
        quader_editor_core::TransformAxis axis,
        float alpha = 1.0f) {
    return highlighted(frame, axis) ? kOverlayToolAxisHighlightColor : withAlpha(colorForAxis(axis), alpha);
}

float visualWidthScale(const quader_editor_core::TransformGizmoFrame& frame) {
    return std::clamp(static_cast<float>(frame.info.gizmo_scale), 0.75f, 1.5f);
}

bool dragAxisIncludes(quader_editor_core::TransformAxis activeAxis, quader_editor_core::TransformAxis axis) {
    using quader_editor_core::TransformAxis;
    if (activeAxis == TransformAxis::none || activeAxis == axis) {
        return true;
    }
    if (activeAxis == TransformAxis::xy) {
        return axis == TransformAxis::x || axis == TransformAxis::y;
    }
    if (activeAxis == TransformAxis::xz) {
        return axis == TransformAxis::x || axis == TransformAxis::z;
    }
    if (activeAxis == TransformAxis::yz) {
        return axis == TransformAxis::y || axis == TransformAxis::z;
    }
    return false;
}

bool dragPlaneIncludes(quader_editor_core::TransformAxis activeAxis, quader_editor_core::TransformAxis planeAxis) {
    return activeAxis == quader_editor_core::TransformAxis::none || activeAxis == planeAxis;
}

bool tiny(QVec3 value) {
    return length_squared(value) <= 0.000001f;
}

QVec3 safeNormalize(QVec3 value, QVec3 fallback = {0.0f, 0.0f, 1.0f}) {
    if (tiny(value)) {
        return fallback;
    }
    return normalize(value);
}

QVec3 fallbackPerpendicular(QVec3 axis) {
    QVec3 side = cross(axis, QVec3{0.0f, 1.0f, 0.0f});
    if (tiny(side)) {
        side = cross(axis, QVec3{1.0f, 0.0f, 0.0f});
    }
    if (tiny(side)) {
        return {0.0f, 0.0f, 1.0f};
    }
    return normalize(side);
}

float signedArea(const std::array<QVec2, 4>& points) {
    float area = 0.0f;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const QVec2 a = points[index];
        const QVec2 b = points[(index + 1u) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

ViewportOverlayLine gizmoLine(
        QVec3 start,
        QVec3 end,
        std::uint32_t color,
        ViewportOverlayLayer layer,
        float widthPixels) {
    return {
            .layer = layer,
            .start = start,
            .end = end,
            .color = color,
            .widthPixels = widthPixels,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    };
}

void appendLine(
        ViewportOverlayFrame& overlayFrame,
        QVec3 start,
        QVec3 end,
        std::uint32_t color,
        ViewportOverlayLayer layer,
        float widthPixels,
        bool underpaint = true) {
    const float clampedWidth = std::max(widthPixels, 1.0f);
    if (underpaint) {
        overlayFrame.lines.push_back(gizmoLine(
                start,
                end,
                kOverlayToolLineShadowColor,
                layer,
                clampedWidth + 1.6f));
    }
    overlayFrame.lines.push_back(gizmoLine(start, end, color, layer, clampedWidth));
}

void appendTriangle(
        ViewportOverlayFrame& overlayFrame,
        QVec3 a,
        QVec3 b,
        QVec3 c,
        std::uint32_t color,
        ViewportOverlayLayer layer = ViewportOverlayLayer::ToolHandle) {
    overlayFrame.triangles.push_back({
            .layer = layer,
            .a = a,
            .b = b,
            .c = c,
            .color = color,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    });
}

float axisWorldLength(const quader_editor_core::TransformGizmoAxisPrimitive& axis) {
    return length(axis.world_tip - axis.world_start);
}

float firstAxisWorldLength(const quader_editor_core::TransformGizmoFrame& frame) {
    if (frame.axes.empty()) {
        return 0.001f;
    }
    return std::max(axisWorldLength(frame.axes.front()), 0.001f);
}

void appendPlane(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoPlanePrimitive& plane,
        std::uint32_t color) {
    const std::array<QVec2, 4> screenPoints{{
            plane.screen0,
            plane.screen1,
            plane.screen2,
            plane.screen3,
    }};
    if (std::abs(signedArea(screenPoints)) <= 2.0f) {
        return;
    }

    appendTriangle(overlayFrame, plane.world0, plane.world1, plane.world2, color, ViewportOverlayLayer::ToolPlane);
    appendTriangle(overlayFrame, plane.world0, plane.world2, plane.world3, color, ViewportOverlayLayer::ToolPlane);
    appendLine(overlayFrame, plane.world0, plane.world1, color, ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
    appendLine(overlayFrame, plane.world1, plane.world2, color, ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
    appendLine(overlayFrame, plane.world2, plane.world3, color, ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
    appendLine(overlayFrame, plane.world3, plane.world0, color, ViewportOverlayLayer::ToolPlaneLine, 1.2f, false);
}

bool appendConeHandle(
        ViewportOverlayFrame& overlayFrame,
        QVec3 start,
        QVec3 tip,
        std::uint32_t color,
        float sizeScale) {
    QVec3 axis = tip - start;
    const float axisLength = length(axis);
    if (axisLength <= 0.000001f) {
        return false;
    }
    axis = axis / axisLength;
    const float coneLength = axisLength * 0.22f * sizeScale;
    const float radius = coneLength * 0.38f;
    QVec3 u = fallbackPerpendicular(axis);
    QVec3 v = cross(axis, u);
    if (tiny(v)) {
        v = fallbackPerpendicular(u);
    } else {
        v = normalize(v);
    }
    u = normalize(cross(v, axis));
    const QVec3 baseCenter = tip - axis * coneLength;

    std::array<QVec3, kConeSegments> basePoints = {};
    for (int i = 0; i < kConeSegments; ++i) {
        const float angle = kCircleTau * static_cast<float>(i) / static_cast<float>(kConeSegments);
        basePoints[static_cast<std::size_t>(i)] = baseCenter + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    }

    for (int i = 0; i < kConeSegments; ++i) {
        const int next = (i + 1) % kConeSegments;
        const QVec3 a = basePoints[static_cast<std::size_t>(i)];
        const QVec3 b = basePoints[static_cast<std::size_t>(next)];
        appendTriangle(overlayFrame, tip, a, b, color);
        appendLine(overlayFrame, a, b, color, ViewportOverlayLayer::ToolHandle, 1.0f);
    }
    appendLine(overlayFrame, baseCenter, tip, color, ViewportOverlayLayer::ToolHandle, 1.0f);
    return true;
}

void appendCubeFaceOutline(
        ViewportOverlayFrame& overlayFrame,
        const std::array<QVec3, 8>& corners,
        const std::array<int, 4>& face,
        std::uint32_t color) {
    appendLine(overlayFrame, corners[static_cast<std::size_t>(face[0])], corners[static_cast<std::size_t>(face[1])], color, ViewportOverlayLayer::ToolHandle, 1.0f, false);
    appendLine(overlayFrame, corners[static_cast<std::size_t>(face[1])], corners[static_cast<std::size_t>(face[2])], color, ViewportOverlayLayer::ToolHandle, 1.0f, false);
    appendLine(overlayFrame, corners[static_cast<std::size_t>(face[2])], corners[static_cast<std::size_t>(face[3])], color, ViewportOverlayLayer::ToolHandle, 1.0f, false);
    appendLine(overlayFrame, corners[static_cast<std::size_t>(face[3])], corners[static_cast<std::size_t>(face[0])], color, ViewportOverlayLayer::ToolHandle, 1.0f, false);
}

void appendCubeHandle(
        ViewportOverlayFrame& overlayFrame,
        const ViewportOverlayCamera& camera,
        QVec3 center,
        float worldSize,
        std::uint32_t color,
        bool hovered) {
    if (worldSize <= 0.000001f) {
        return;
    }

    const float half = worldSize * 0.5f;
    const std::array<QVec3, 8> corners{{
            center + QVec3{-half, -half, -half},
            center + QVec3{half, -half, -half},
            center + QVec3{half, half, -half},
            center + QVec3{-half, half, -half},
            center + QVec3{-half, -half, half},
            center + QVec3{half, -half, half},
            center + QVec3{half, half, half},
            center + QVec3{-half, half, half},
    }};
    constexpr std::array<std::array<int, 4>, 6> faces{{
            {{1, 5, 6, 2}},
            {{4, 0, 3, 7}},
            {{3, 2, 6, 7}},
            {{4, 5, 1, 0}},
            {{5, 4, 7, 6}},
            {{0, 1, 2, 3}},
    }};
    const std::array<QVec3, 6> normals{{
            {1.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f},
    }};

    const QVec3 viewDirection = safeNormalize(camera.eye - center, {0.0f, 0.0f, 1.0f});
    const QVec3 lightDirection = safeNormalize({-0.45f, -0.65f, 0.62f});

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const float facing = dot(normals[faceIndex], viewDirection);
        if (facing <= 0.000001f) {
            continue;
        }
        const float lit = std::max(dot(normals[faceIndex], lightDirection), 0.0f);
        const float shade = std::clamp(0.46f + lit * 0.36f + std::min(facing, 1.0f) * 0.18f, 0.42f, 1.12f);
        const std::uint32_t fillColor = multiplyColor(color, hovered ? shade * 1.15f : shade);
        const std::array<int, 4>& face = faces[faceIndex];
        appendTriangle(
                overlayFrame,
                corners[static_cast<std::size_t>(face[0])],
                corners[static_cast<std::size_t>(face[1])],
                corners[static_cast<std::size_t>(face[2])],
                fillColor);
        appendTriangle(
                overlayFrame,
                corners[static_cast<std::size_t>(face[0])],
                corners[static_cast<std::size_t>(face[2])],
                corners[static_cast<std::size_t>(face[3])],
                fillColor);
        if (hovered) {
            appendCubeFaceOutline(overlayFrame, corners, face, rgba8(255, 246, 128, 180));
        }
    }
}

void appendCenterCircle(
        ViewportOverlayFrame& overlayFrame,
        const ViewportOverlayCamera& camera,
        QVec3 center,
        float radiusWorld,
        std::uint32_t color) {
    if (radiusWorld <= 0.000001f) {
        return;
    }
    const QVec3 right = safeNormalize(camera.right, {1.0f, 0.0f, 0.0f});
    const QVec3 up = safeNormalize(camera.up, {0.0f, 1.0f, 0.0f});
    for (int i = 0; i < kCenterCircleSegments; ++i) {
        const float a0 = kCircleTau * static_cast<float>(i) / static_cast<float>(kCenterCircleSegments);
        const float a1 = kCircleTau * static_cast<float>(i + 1) / static_cast<float>(kCenterCircleSegments);
        const QVec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * radiusWorld;
        const QVec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * radiusWorld;
        appendTriangle(overlayFrame, center, p0, p1, color);
    }
}

void appendRotateGizmo(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoFrame& gizmoFrame,
        float widthScale) {
    const float trackballWidth = 2.65f * widthScale;
    for (const quader_editor_core::TransformGizmoTrackballSegmentPrimitive& segment : gizmoFrame.trackball_segments) {
        appendLine(
                overlayFrame,
                segment.world_start,
                segment.world_end,
                kOverlayToolTrackballColor,
                ViewportOverlayLayer::ToolHandle,
                trackballWidth);
    }

    for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment : gizmoFrame.ring_segments) {
        if (!segment.front_facing) {
            continue;
        }
        appendLine(
                overlayFrame,
                segment.world_start,
                segment.world_end,
                authoredAxisColor(gizmoFrame, segment.axis, segment.alpha),
                ViewportOverlayLayer::ToolHandle,
                3.45f * widthScale);
    }
}

void appendMoveOrScaleGizmo(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoFrame& gizmoFrame,
        const ViewportOverlayCamera& camera,
        float widthScale) {
    for (const quader_editor_core::TransformGizmoPlanePrimitive& plane : gizmoFrame.planes) {
        if (!dragPlaneIncludes(gizmoFrame.info.active_axis, plane.axis)) {
            continue;
        }
        const std::uint32_t color =
                highlighted(gizmoFrame, plane.axis) ? kOverlayToolAxisHighlightColor : colorForPlane(plane.axis);
        appendPlane(overlayFrame, plane, color);
    }

    for (const quader_editor_core::TransformGizmoAxisPrimitive& axis : gizmoFrame.axes) {
        if (!dragAxisIncludes(gizmoFrame.info.active_axis, axis.axis)) {
            continue;
        }
        const bool isHighlighted = highlighted(gizmoFrame, axis.axis);
        const std::uint32_t color = isHighlighted
                ? kOverlayToolAxisHighlightColor
                : withAlpha(colorForAxis(axis.axis), axis.alpha);
        appendLine(
                overlayFrame,
                axis.world_start,
                axis.world_tip,
                color,
                ViewportOverlayLayer::ToolHandle,
                2.25f * widthScale);

        const float axisLength = std::max(axisWorldLength(axis), 0.0001f);
        if (gizmoFrame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
            const float worldSize = std::max(0.0001f, axisLength * (kScaleHandleSizePixels / kOldGizmoBaseSizePixels));
            appendCubeHandle(overlayFrame, camera, axis.world_tip, worldSize, color, isHighlighted);
        } else {
            (void)appendConeHandle(overlayFrame, axis.world_start, axis.world_tip, color, 1.0f);
        }
    }

    const bool centerHovered =
            gizmoFrame.info.hover_axis == quader_editor_core::TransformAxis::all ||
            gizmoFrame.info.active_axis == quader_editor_core::TransformAxis::all;
    if (gizmoFrame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
        const float worldSize = std::max(
                0.0001f,
                firstAxisWorldLength(gizmoFrame) * (kScaleCenterSizePixels / kOldGizmoBaseSizePixels));
        appendCubeHandle(
                overlayFrame,
                camera,
                gizmoFrame.info.pivot,
                worldSize,
                centerHovered ? kOverlayToolAxisHighlightColor : kOverlayToolCenterColor,
                centerHovered);
    } else {
        const float radiusWorld =
                firstAxisWorldLength(gizmoFrame) * (kMoveCenterRadiusPixels / kOldGizmoBaseSizePixels);
        appendCenterCircle(overlayFrame, camera, gizmoFrame.info.pivot, radiusWorld, kOverlayToolCenterColor);
    }
}

} // namespace

void ViewportTransformGizmoOverlayBuilder::append(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoFrame& gizmoFrame,
        const ViewportOverlayCamera& camera) {
    if (!gizmoFrame.info.ok) {
        return;
    }

    const float widthScale = visualWidthScale(gizmoFrame);
    if (gizmoFrame.info.tool == quader_editor_core::ViewportInteractionTool::rotate) {
        appendRotateGizmo(overlayFrame, gizmoFrame, widthScale);
        return;
    }

    appendMoveOrScaleGizmo(overlayFrame, gizmoFrame, camera, widthScale);
}

} // namespace quader
