////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quader {

inline constexpr float kViewportGridDefaultPlaneSize = 8192.0f;
inline constexpr float kViewportGridPlaneOffset = -0.002f;
inline constexpr float kViewportGridMinOrthographicSize = 0.01f;
inline constexpr float kViewportGridOrthographicMargin = 1.25f;

/**
 * Enumerates ViewportGridPlane values used by the viewport layer.
 */
enum class ViewportGridPlane {
    XZ,
    YZ,
    XY,
};

/**
 * Enumerates ViewportGridRotation values used by the viewport layer.
 */
enum class ViewportGridRotation {
    Identity,
    NegativeHalfPiAroundZ,
    PositiveHalfPiAroundX,
};

/**
 * Represents a Viewport Grid Axes value used by the viewport input, layout, and camera layer.
 */
struct ViewportGridAxes {
    QVec3 uAxis = {1.0f, 0.0f, 0.0f};
    QVec3 vAxis = {0.0f, 0.0f, -1.0f};
    ViewportColorSettingId uColor = ViewportColorSettingId::GridXAxis;
    ViewportColorSettingId vColor = ViewportColorSettingId::GridZAxis;
};

/**
 * Represents a Viewport Grid Placement value used by the viewport input, layout, and camera layer.
 */
struct ViewportGridPlacement {
    ViewportGridPlane plane = ViewportGridPlane::XZ;
    QVec3 origin = {};
    ViewportGridRotation rotation = ViewportGridRotation::Identity;
    QVec2 planeSize = {kViewportGridDefaultPlaneSize, kViewportGridDefaultPlaneSize};
    ViewportGridAxes axes = {};
};

/**
 * Owns Viewport Grid Placement Policy behavior within the viewport input, layout, and camera layer.
 */
class ViewportGridPlacementPolicy {
public:
    static ViewportGridPlacement placement(
            const ViewportCameraController& camera,
            std::uint32_t viewportWidth,
            std::uint32_t viewportHeight) {
        const ViewportCameraPose pose = camera.pose();
        const ViewportGridPlane resolvedPlane = plane(camera, pose);
        return {
                .plane = resolvedPlane,
                .origin = origin(resolvedPlane, pose),
                .rotation = rotation(resolvedPlane),
                .planeSize = planeSize(resolvedPlane, camera, viewportWidth, viewportHeight),
                .axes = axes(resolvedPlane),
        };
    }

    static ViewportGridPlane plane(const ViewportCameraController& camera, const ViewportCameraPose& pose) {
        if (camera.state().projection != ViewportProjection::Orthographic) {
            return ViewportGridPlane::XZ;
        }

        QVec3 forward = normalize(pose.target - pose.eye);
        if (length(forward) <= 0.000001f) {
            forward = {0.0f, 0.0f, 1.0f};
        }

        const float absX = std::fabs(forward.x);
        const float absY = std::fabs(forward.y);
        const float absZ = std::fabs(forward.z);
        if (absX >= absY && absX >= absZ) {
            return ViewportGridPlane::YZ;
        }
        if (absZ >= absX && absZ >= absY) {
            return ViewportGridPlane::XY;
        }
        return ViewportGridPlane::XZ;
    }

    static QVec2 planeSize(
            ViewportGridPlane plane,
            const ViewportCameraController& camera,
            std::uint32_t viewportWidth,
            std::uint32_t viewportHeight) {
        if (camera.state().projection != ViewportProjection::Orthographic) {
            return {kViewportGridDefaultPlaneSize, kViewportGridDefaultPlaneSize};
        }

        const float aspect =
                std::max(0.0001f, static_cast<float>(std::max(1u, viewportWidth)) / static_cast<float>(std::max(1u, viewportHeight)));
        const float planeHeight =
                std::max(kViewportGridMinOrthographicSize, camera.state().orthographicSize) * kViewportGridOrthographicMargin;
        if (plane == ViewportGridPlane::YZ) {
            return {planeHeight, planeHeight * aspect};
        }
        return {planeHeight * aspect, planeHeight};
    }

    static ViewportGridAxes axes(ViewportGridPlane plane) {
        if (plane == ViewportGridPlane::YZ) {
            return {
                    .uAxis = {0.0f, 0.0f, -1.0f},
                    .vAxis = {0.0f, 1.0f, 0.0f},
                    .uColor = ViewportColorSettingId::GridZAxis,
                    .vColor = ViewportColorSettingId::GridYAxis,
            };
        }
        if (plane == ViewportGridPlane::XY) {
            return {
                    .uAxis = {1.0f, 0.0f, 0.0f},
                    .vAxis = {0.0f, 1.0f, 0.0f},
                    .uColor = ViewportColorSettingId::GridXAxis,
                    .vColor = ViewportColorSettingId::GridYAxis,
            };
        }
        return {};
    }

    static QVec3 origin(ViewportGridPlane plane, const ViewportCameraPose& pose) {
        if (plane == ViewportGridPlane::YZ) {
            return {kViewportGridPlaneOffset, pose.eye.y, pose.eye.z};
        }
        if (plane == ViewportGridPlane::XY) {
            return {pose.eye.x, pose.eye.y, kViewportGridPlaneOffset};
        }
        return {pose.eye.x, kViewportGridPlaneOffset, pose.eye.z};
    }

    static ViewportGridRotation rotation(ViewportGridPlane plane) {
        if (plane == ViewportGridPlane::YZ) {
            return ViewportGridRotation::NegativeHalfPiAroundZ;
        }
        if (plane == ViewportGridPlane::XY) {
            return ViewportGridRotation::PositiveHalfPiAroundX;
        }
        return ViewportGridRotation::Identity;
    }
};

} // namespace quader
