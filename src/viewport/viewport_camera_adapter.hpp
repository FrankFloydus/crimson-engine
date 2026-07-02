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

    static ViewportOverlayCamera
    overlay_camera(const ViewportCameraController &camera,
                   QVec2 viewport_size) {
      const ViewportCameraBasis camera_basis = basis(camera);
      const ViewportCameraState &state = camera.state();
      return {
          .eye = camera_basis.eye,
          .forward = camera_basis.forward,
          .right = camera_basis.right,
          .up = camera_basis.up,
          .viewport_size = {std::max(viewport_size.x, 1.0f),
                            std::max(viewport_size.y, 1.0f)},
          .projection = state.projection == ViewportProjection::Orthographic
                            ? ViewportOverlayProjection::Orthographic
                            : ViewportOverlayProjection::Perspective,
          .orthographic_size = state.orthographic_size,
          .fov_degrees = state.fov_degrees,
          .near_plane = state.near_plane,
          .far_plane = state.far_plane,
      };
    }

    static ModelingViewportCamera
    modeling_camera(const ViewportCameraController &camera,
                    QVec2 viewport_size) {
      const ViewportCameraBasis camera_basis = basis(camera);
      const ViewportCameraState &state = camera.state();
      return {
          .orthographic = state.projection == ViewportProjection::Orthographic,
          .origin = camera_basis.eye,
          .x_axis = camera_basis.right,
          .y_axis = camera_basis.up,
          .z_axis = camera_basis.forward * -1.0f,
          .viewport_size = viewport_size,
          .orthographic_size = state.orthographic_size,
          .fov_degrees = state.fov_degrees,
      };
    }

    static quader_editor_core::ViewportCameraState
    runtime_camera(const ViewportCameraController &camera,
                   QVec2 viewport_size) {
      const ViewportCameraBasis camera_basis = basis(camera);
      const ViewportCameraState &state = camera.state();
      quader_editor_core::ViewportCameraState runtime_camera;
      runtime_camera.projection =
          state.projection == ViewportProjection::Orthographic
              ? quader_editor_core::CameraProjection::Orthographic
              : quader_editor_core::CameraProjection::Perspective;
      runtime_camera.transform.origin = camera_basis.eye;
      runtime_camera.transform.x_axis = camera_basis.right;
      runtime_camera.transform.y_axis = camera_basis.up;
      runtime_camera.transform.z_axis = camera_basis.forward * -1.0f;
      runtime_camera.viewport_size = {std::max(viewport_size.x, 1.0f),
                                      std::max(viewport_size.y, 1.0f)};
      runtime_camera.orthographic_size =
          std::max(state.orthographic_size, 0.0001f);
      runtime_camera.fov_degrees = state.fov_degrees;
      return runtime_camera;
    }
};

} // namespace quader
