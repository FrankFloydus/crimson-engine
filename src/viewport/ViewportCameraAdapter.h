#pragma once

#include "modeling/ModelingSessionService.h"
#include "render/ViewportOverlayFrame.h"
#include "viewport/ViewportCameraController.h"

#include <algorithm>

namespace quader {

struct ViewportCameraBasis {
    Vec3 eye = {};
    Vec3 forward = {0.0f, 0.0f, -1.0f};
    Vec3 right = {1.0f, 0.0f, 0.0f};
    Vec3 up = {0.0f, 1.0f, 0.0f};
};

class ViewportCameraAdapter {
public:
    static ViewportCameraBasis basis(const ViewportCameraController& camera) {
        const ViewportCameraPose pose = camera.pose();
        Vec3 forward = normalize(pose.target - pose.eye);
        if (length(forward) <= 0.000001f) {
            forward = {0.0f, 0.0f, -1.0f};
        }

        Vec3 right = normalize(cross(forward, pose.up));
        if (length(right) <= 0.000001f) {
            right = {1.0f, 0.0f, 0.0f};
        }

        Vec3 up = normalize(cross(right, forward));
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

    static ViewportOverlayCamera overlayCamera(const ViewportCameraController& camera, Vec2 viewportSize) {
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

    static ModelingViewportCamera modelingCamera(const ViewportCameraController& camera, Vec2 viewportSize) {
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
};

} // namespace quader
