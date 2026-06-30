////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"

#include "render/overlay/viewport_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <editor/interactions/transform_gizmo.hpp>

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCircleTau = kPi * 2.0f;
constexpr float kGizmoCircleSize = 1.1f;
constexpr float kGizmoArrowSize = 0.35f;
constexpr float kGizmoArrowOffset = kGizmoCircleSize + 0.3f;
constexpr float kGizmoArrowTip = kGizmoArrowOffset + kGizmoArrowSize;
constexpr float kGizmoArrowConeRadius = 0.065f;
constexpr float kGizmoScaleOffset = kGizmoCircleSize + 0.3f;
constexpr float kGizmoScaleTip = kGizmoScaleOffset * 1.11f;
constexpr float kGizmoScaleHalfWidth = 0.07f;
constexpr int kConeSegments = 16;

std::uint8_t channel(std::uint32_t color, int shift) {
    return static_cast<std::uint8_t>((color >> shift) & 0xffu);
}

std::uint8_t scaledByte(std::uint8_t value, float scale) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<float>(value) * scale), 0l, 255l));
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
    case quader_editor_core::TransformAxis::xz:
        return kOverlayToolPlaneXZColor;
    case quader_editor_core::TransformAxis::yz:
        return kOverlayToolPlaneYZColor;
    default:
        return rgba8(255, 255, 255, 255);
    }
}

std::uint32_t highlightColorForAxis(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
    case quader_editor_core::TransformAxis::yz:
        return kOverlayToolAxisXHighlightColor;
    case quader_editor_core::TransformAxis::y:
    case quader_editor_core::TransformAxis::xz:
        return kOverlayToolAxisYHighlightColor;
    case quader_editor_core::TransformAxis::z:
    case quader_editor_core::TransformAxis::xy:
        return kOverlayToolAxisZHighlightColor;
    case quader_editor_core::TransformAxis::all:
    case quader_editor_core::TransformAxis::none:
        return kOverlayToolAxisHighlightColor;
    }
    return kOverlayToolAxisHighlightColor;
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
    return highlighted(frame, axis) ? highlightColorForAxis(axis) : multiplyColor(colorForAxis(axis), 1.0f, alpha);
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
        bool underpaint = false) {
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

void appendQuad(
        ViewportOverlayFrame& overlayFrame,
        QVec3 a,
        QVec3 b,
        QVec3 c,
        QVec3 d,
        std::uint32_t color,
        ViewportOverlayLayer layer = ViewportOverlayLayer::ToolHandle) {
    appendTriangle(overlayFrame, a, b, c, color, layer);
    appendTriangle(overlayFrame, a, c, d, color, layer);
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

float rotationRingRadiusWorld(
        const quader_editor_core::TransformGizmoFrame& frame,
        quader_editor_core::TransformAxis axis) {
    float radius = 0.0f;
    for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment : frame.ring_segments) {
        if (segment.axis != axis) {
            continue;
        }
        radius = std::max(radius, length(segment.world_start - frame.info.pivot));
        radius = std::max(radius, length(segment.world_end - frame.info.pivot));
    }
    return std::max(radius, 0.001f);
}

QVec3 projectedOntoPlane(QVec3 value, QVec3 normal) {
    return value - normal * dot(value, normal);
}

QVec3 rotateAroundAxis(QVec3 value, QVec3 axis, float angleRadians) {
    const float c = std::cos(angleRadians);
    const float s = std::sin(angleRadians);
    return value * c + cross(axis, value) * s + axis * (dot(axis, value) * (1.0f - c));
}

QVec3 normalizedRotateIndicatorStartDirection(
        const quader_editor_core::TransformGizmoRotateIndicator& indicator) {
    const QVec3 start = projectedOntoPlane(indicator.start_direction, indicator.world_axis);
    const float startLength = length(start);
    if (startLength > 0.000001f) {
        return start / startLength;
    }
    return viewportOverlayFallbackPerpendicular(indicator.world_axis);
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

void appendConeHandle(
        ViewportOverlayFrame& overlayFrame,
        QVec3 baseCenter,
        QVec3 tip,
        std::uint32_t color,
        float radiusWorld) {
    QVec3 axis = tip - baseCenter;
    const float coneLength = length(axis);
    if (coneLength <= 0.000001f || radiusWorld <= 0.000001f) {
        return;
    }
    axis = axis / coneLength;
    QVec3 u = viewportOverlayFallbackPerpendicular(axis);
    QVec3 v = cross(axis, u);
    if (viewportOverlayVec3Tiny(v)) {
        v = viewportOverlayFallbackPerpendicular(u);
    } else {
        v = normalize(v);
    }
    u = normalize(cross(v, axis));

    std::array<QVec3, kConeSegments> basePoints = {};
    for (int i = 0; i < kConeSegments; ++i) {
        const float angle = kCircleTau * static_cast<float>(i) / static_cast<float>(kConeSegments);
        basePoints[static_cast<std::size_t>(i)] = baseCenter + (u * std::cos(angle) + v * std::sin(angle)) * radiusWorld;
    }

    for (int i = 0; i < kConeSegments; ++i) {
        const int next = (i + 1) % kConeSegments;
        const QVec3 a = basePoints[static_cast<std::size_t>(i)];
        const QVec3 b = basePoints[static_cast<std::size_t>(next)];
        appendTriangle(overlayFrame, tip, a, b, color);
        appendTriangle(overlayFrame, baseCenter, b, a, color);
    }
}

void appendArrowHandle(
        ViewportOverlayFrame& overlayFrame,
        QVec3 start,
        QVec3 tip,
        std::uint32_t color,
        float lineWidthPixels) {
    QVec3 direction = tip - start;
    const float axisLength = length(direction);
    if (axisLength <= 0.000001f) {
        return;
    }
    direction = direction / axisLength;
    const float unitWorld = axisLength / kGizmoArrowTip;
    const QVec3 coneBase = start + direction * (unitWorld * kGizmoArrowOffset);
    appendLine(overlayFrame, start, coneBase, color, ViewportOverlayLayer::ToolHandle, lineWidthPixels);
    appendConeHandle(overlayFrame, coneBase, tip, color, unitWorld * kGizmoArrowConeRadius);
}

void appendSquarePrism(
        ViewportOverlayFrame& overlayFrame,
        QVec3 start,
        QVec3 end,
        float halfWidthWorld,
        std::uint32_t color) {
    QVec3 axis = end - start;
    const float lengthWorld = length(axis);
    if (lengthWorld <= 0.000001f || halfWidthWorld <= 0.000001f) {
        return;
    }
    axis = axis / lengthWorld;
    QVec3 sideA = viewportOverlayFallbackPerpendicular(axis);
    QVec3 sideB = viewportOverlayNormalizeOr(cross(axis, sideA), {0.0f, 1.0f, 0.0f});
    sideA = viewportOverlayNormalizeOr(cross(sideB, axis), {1.0f, 0.0f, 0.0f});

    const std::array<QVec3, 4> startCorners{{
            start + sideA * halfWidthWorld + sideB * halfWidthWorld,
            start - sideA * halfWidthWorld + sideB * halfWidthWorld,
            start - sideA * halfWidthWorld - sideB * halfWidthWorld,
            start + sideA * halfWidthWorld - sideB * halfWidthWorld,
    }};
    const std::array<QVec3, 4> endCorners{{
            end + sideA * halfWidthWorld + sideB * halfWidthWorld,
            end - sideA * halfWidthWorld + sideB * halfWidthWorld,
            end - sideA * halfWidthWorld - sideB * halfWidthWorld,
            end + sideA * halfWidthWorld - sideB * halfWidthWorld,
    }};

    for (int index = 0; index < 4; ++index) {
        const int next = (index + 1) % 4;
        appendQuad(
                overlayFrame,
                startCorners[static_cast<std::size_t>(index)],
                startCorners[static_cast<std::size_t>(next)],
                endCorners[static_cast<std::size_t>(next)],
                endCorners[static_cast<std::size_t>(index)],
                color);
    }
    appendQuad(overlayFrame, startCorners[0], startCorners[1], startCorners[2], startCorners[3], color);
    appendQuad(overlayFrame, endCorners[3], endCorners[2], endCorners[1], endCorners[0], color);
}

void appendScaleHandle(
        ViewportOverlayFrame& overlayFrame,
        QVec3 start,
        QVec3 tip,
        std::uint32_t color,
        float lineWidthPixels) {
    QVec3 direction = tip - start;
    const float axisLength = length(direction);
    if (axisLength <= 0.000001f) {
        return;
    }
    direction = direction / axisLength;
    const float unitWorld = axisLength / kGizmoScaleTip;
    const QVec3 blockStart = start + direction * (unitWorld * kGizmoScaleOffset);
    appendLine(overlayFrame, start, blockStart, color, ViewportOverlayLayer::ToolHandle, lineWidthPixels);
    appendSquarePrism(overlayFrame, blockStart, tip, unitWorld * kGizmoScaleHalfWidth, color);
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

    const QVec3 viewDirection = viewportOverlayNormalizeOr(camera.eye - center, {0.0f, 0.0f, 1.0f});
    const QVec3 lightDirection = viewportOverlayNormalizeOr({-0.45f, -0.65f, 0.62f}, {0.0f, 0.0f, 1.0f});

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

void appendRotateIndicator(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoFrame& gizmoFrame) {
    const quader_editor_core::TransformGizmoRotateIndicator& indicator = gizmoFrame.info.rotate_indicator;
    if (!indicator.active ||
            indicator.axis == quader_editor_core::TransformAxis::none ||
            !std::isfinite(indicator.angle_degrees)) {
        return;
    }

    const float axisLength = length(indicator.world_axis);
    if (axisLength <= 0.000001f) {
        return;
    }
    const QVec3 planeNormal = indicator.world_axis / axisLength;
    const QVec3 startDirection = normalizedRotateIndicatorStartDirection(indicator);
    const float angleRadians = static_cast<float>(indicator.angle_degrees * static_cast<double>(kPi) / 180.0);
    const QVec3 currentDirection = viewportOverlayNormalizeOr(
            rotateAroundAxis(startDirection, planeNormal, angleRadians),
            startDirection);
    const float ringRadius = rotationRingRadiusWorld(gizmoFrame, indicator.axis);
    const float pieRadius = ringRadius * 0.98f;
    const int arcSteps = std::clamp(static_cast<int>(std::abs(angleRadians) / (kPi / 24.0f)) + 2, 3, 72);

    QVec3 previousPoint = gizmoFrame.info.pivot + startDirection * pieRadius;
    if (std::abs(angleRadians) > 0.005f) {
        for (int step = 1; step <= arcSteps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(arcSteps);
            const QVec3 direction = rotateAroundAxis(startDirection, planeNormal, angleRadians * t);
            const QVec3 point = gizmoFrame.info.pivot + direction * pieRadius;
            appendTriangle(
                    overlayFrame,
                    gizmoFrame.info.pivot,
                    previousPoint,
                    point,
                    kOverlayTransformGizmoRotatePieFillColor,
                    ViewportOverlayLayer::ToolPreviewFill);
            previousPoint = point;
        }
    }

    const QVec3 startPoint = gizmoFrame.info.pivot + startDirection * pieRadius;
    const QVec3 currentPoint = gizmoFrame.info.pivot + currentDirection * (ringRadius * 1.04f);
    const QVec3 mousePoint = indicator.has_mouse_position ? indicator.mouse_position : currentPoint;
    appendLine(
            overlayFrame,
            gizmoFrame.info.pivot,
            startPoint,
            kOverlayTransformGizmoRotatePieLineColor,
            ViewportOverlayLayer::ToolPreviewLine,
            1.25f,
            false);
    appendLine(
            overlayFrame,
            gizmoFrame.info.pivot,
            mousePoint,
            kOverlayTransformGizmoRotatePieLineColor,
            ViewportOverlayLayer::ToolPreviewLine,
            1.55f,
            false);
}

void appendRotateGizmo(
        ViewportOverlayFrame& overlayFrame,
        const quader_editor_core::TransformGizmoFrame& gizmoFrame,
        float widthScale) {
    const float trackballWidth = 2.25f * widthScale;
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
                authoredAxisColor(gizmoFrame, segment.axis),
                ViewportOverlayLayer::ToolHandle,
                3.0f * widthScale);
    }

    appendRotateIndicator(overlayFrame, gizmoFrame);
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
                highlighted(gizmoFrame, plane.axis) ? highlightColorForAxis(plane.axis) : colorForPlane(plane.axis);
        appendPlane(overlayFrame, plane, color);
    }

    for (const quader_editor_core::TransformGizmoAxisPrimitive& axis : gizmoFrame.axes) {
        if (!dragAxisIncludes(gizmoFrame.info.active_axis, axis.axis)) {
            continue;
        }
        const bool isHighlighted = highlighted(gizmoFrame, axis.axis);
        const std::uint32_t color = isHighlighted
                ? highlightColorForAxis(axis.axis)
                : multiplyColor(colorForAxis(axis.axis), 1.0f, axis.alpha);
        const float axisLineWidth = kOverlayTransformGizmoAxisLineWidthPixels * widthScale;

        if (gizmoFrame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
            appendScaleHandle(overlayFrame, axis.world_start, axis.world_tip, color, axisLineWidth);
        } else {
            appendArrowHandle(overlayFrame, axis.world_start, axis.world_tip, color, axisLineWidth);
        }
    }

    const bool centerHovered =
            gizmoFrame.info.hover_axis == quader_editor_core::TransformAxis::all ||
            gizmoFrame.info.active_axis == quader_editor_core::TransformAxis::all;
    if (gizmoFrame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
        if (centerHovered) {
            const float worldSize = std::max(
                    0.0001f,
                    firstAxisWorldLength(gizmoFrame) *
                            (kOverlayTransformGizmoScaleCenterSizePixels / kOverlayTransformGizmoSizePixels));
            appendCubeHandle(
                    overlayFrame,
                    camera,
                    gizmoFrame.info.pivot,
                    worldSize,
                    kOverlayToolAxisHighlightColor,
                    true);
        }
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
