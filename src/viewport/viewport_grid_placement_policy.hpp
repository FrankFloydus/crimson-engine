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

// A finite construction plane, equivalent to +/-16384 map units at 32:1.
inline constexpr float kViewportGridDefaultPlaneSize = 1024.0f;
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
  QVec3 u_axis = {1.0f, 0.0f, 0.0f};
  QVec3 v_axis = {0.0f, 0.0f, -1.0f};
  ViewportColorSettingId u_color = ViewportColorSettingId::GridXAxis;
  ViewportColorSettingId v_color = ViewportColorSettingId::GridZAxis;
};

/**
 * Represents a Viewport Grid Placement value used by the viewport input, layout, and camera layer.
 */
struct ViewportGridPlacement {
    ViewportGridPlane plane = ViewportGridPlane::XZ;
    QVec3 origin = {};
    ViewportGridRotation rotation = ViewportGridRotation::Identity;
    QVec2 plane_size = {kViewportGridDefaultPlaneSize,
                        kViewportGridDefaultPlaneSize};
    ViewportGridAxes axes = {};
};

/**
 * Owns Viewport Grid Placement Policy behavior within the viewport input, layout, and camera layer.
 */
class ViewportGridPlacementPolicy {
public:
  static ViewportGridPlacement placement(const ViewportCameraController &camera,
                                         std::uint32_t viewport_width,
                                         std::uint32_t viewport_height) {
    const ViewportCameraPose pose = camera.pose();
    const ViewportGridPlane resolved_plane = plane(camera, pose);
    QVec3 plane_origin = origin(resolved_plane, pose);
    if (camera.state().projection == ViewportProjection::Perspective) {
      // The construction grid is a bounded world plane. Its perimeter must
      // not move with the camera; coplanar depth bias belongs to the material.
      plane_origin = {};
    }
    return {
        .plane = resolved_plane,
        .origin = plane_origin,
        .rotation = rotation(resolved_plane),
        .plane_size =
            plane_size(resolved_plane, camera, viewport_width, viewport_height),
        .axes = axes(resolved_plane),
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

        const float abs_x = std::fabs(forward.x);
        const float abs_y = std::fabs(forward.y);
        const float abs_z = std::fabs(forward.z);
        if (abs_x >= abs_y && abs_x >= abs_z) {
          return ViewportGridPlane::YZ;
        }
        if (abs_z >= abs_x && abs_z >= abs_y) {
          return ViewportGridPlane::XY;
        }
        return ViewportGridPlane::XZ;
    }

    static QVec2 plane_size(ViewportGridPlane plane,
                            const ViewportCameraController &camera,
                            std::uint32_t viewport_width,
                            std::uint32_t viewport_height) {
      if (camera.state().projection != ViewportProjection::Orthographic) {
        return {kViewportGridDefaultPlaneSize, kViewportGridDefaultPlaneSize};
      }

      const float aspect = std::max(
          0.0001f, static_cast<float>(std::max(1u, viewport_width)) /
                       static_cast<float>(std::max(1u, viewport_height)));
      const float plane_height = std::max(kViewportGridMinOrthographicSize,
                                          camera.state().orthographic_size) *
                                 kViewportGridOrthographicMargin;
      if (plane == ViewportGridPlane::YZ) {
        return {plane_height, plane_height * aspect};
      }
      return {plane_height * aspect, plane_height};
    }

    static ViewportGridAxes axes(ViewportGridPlane plane) {
        if (plane == ViewportGridPlane::YZ) {
          return {
              .u_axis = {0.0f, 0.0f, -1.0f},
              .v_axis = {0.0f, 1.0f, 0.0f},
              .u_color = ViewportColorSettingId::GridZAxis,
              .v_color = ViewportColorSettingId::GridYAxis,
          };
        }
        if (plane == ViewportGridPlane::XY) {
          return {
              .u_axis = {1.0f, 0.0f, 0.0f},
              .v_axis = {0.0f, 1.0f, 0.0f},
              .u_color = ViewportColorSettingId::GridXAxis,
              .v_color = ViewportColorSettingId::GridYAxis,
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
