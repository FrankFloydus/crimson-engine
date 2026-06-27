////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>

#include <editor/interactions/viewport_projection.hpp>

namespace quader {

/**
 * Represents a Viewport Camera Basis value used by the viewport input, layout, and camera layer.
 */
struct ViewportCameraBasis {
    QVec3 eye = {};
    QVec3 forward = {0.0f, 0.0f, -1.0f};
    QVec3 right = {1.0f, 0.0f, 0.0f};
    QVec3 up = {0.0f, 1.0f, 0.0f};
};

/**
 * Adapts Viewport Camera Adapter data between Quader layers for the viewport input, layout, and camera layer.
 */
class ViewportCameraAdapter {
public:
    static ViewportCameraBasis basis(const ViewportCameraController& camera) {
        const ViewportCameraPose pose = camera.pose();
        QVec3 forward = normalize(pose.target - pose.eye);
        if (length(forward) <= 0.000001f) {
            forward = {0.0f, 0.0f, -1.0f};
        }

        QVec3 right = normalize(cross(forward, pose.up));
        if (length(right) <= 0.000001f) {
            right = {1.0f, 0.0f, 0.0f};
        }

        QVec3 up = normalize(cross(right, forward));
        if (length(up) <= 0.000001f) {
            up = {0.0f, 1.0f, 0.0f};
        }

        return {
                .eye = pose.eye,
                .forward = forward,
                .right = right,
                .up = up,
        };
    }

    static ViewportOverlayCamera overlayCamera(const ViewportCameraController& camera, QVec2 viewportSize) {
        const ViewportCameraBasis cameraBasis = basis(camera);
        const ViewportCameraState& state = camera.state();
        return {
                .eye = cameraBasis.eye,
                .forward = cameraBasis.forward,
                .right = cameraBasis.right,
                .up = cameraBasis.up,
                .viewportSize = {std::max(viewportSize.x, 1.0f), std::max(viewportSize.y, 1.0f)},
                .projection = state.projection == ViewportProjection::Orthographic
                        ? ViewportOverlayProjection::Orthographic
                        : ViewportOverlayProjection::Perspective,
                .orthographicSize = state.orthographicSize,
                .fovDegrees = state.fovDegrees,
                .nearPlane = state.nearPlane,
                .farPlane = state.farPlane,
        };
    }

    static ModelingViewportCamera modelingCamera(const ViewportCameraController& camera, QVec2 viewportSize) {
        const ViewportCameraBasis cameraBasis = basis(camera);
        const ViewportCameraState& state = camera.state();
        return {
                .orthographic = state.projection == ViewportProjection::Orthographic,
                .origin = cameraBasis.eye,
                .xAxis = cameraBasis.right,
                .yAxis = cameraBasis.up,
                .zAxis = cameraBasis.forward * -1.0f,
                .viewportSize = viewportSize,
                .orthographicSize = state.orthographicSize,
                .fovDegrees = state.fovDegrees,
        };
    }

    static quader_editor_core::ViewportCameraState runtimeCamera(const ViewportCameraController& camera, QVec2 viewportSize) {
        const ViewportCameraBasis cameraBasis = basis(camera);
        const ViewportCameraState& state = camera.state();
        quader_editor_core::ViewportCameraState runtimeCamera;
        runtimeCamera.projection = state.projection == ViewportProjection::Orthographic
                ? quader_editor_core::CameraProjection::orthographic
                : quader_editor_core::CameraProjection::perspective;
        runtimeCamera.transform.origin = cameraBasis.eye;
        runtimeCamera.transform.x_axis = cameraBasis.right;
        runtimeCamera.transform.y_axis = cameraBasis.up;
        runtimeCamera.transform.z_axis = cameraBasis.forward * -1.0f;
        runtimeCamera.viewport_size = {std::max(viewportSize.x, 1.0f), std::max(viewportSize.y, 1.0f)};
        runtimeCamera.orthographic_size = std::max(state.orthographicSize, 0.0001f);
        runtimeCamera.fov_degrees = state.fovDegrees;
        return runtimeCamera;
    }
};

} // namespace quader
