////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/camera_pose_validation.hpp"

namespace quader {

/**
 * Stores one QDR-owned three-component value without coupling persistence DTOs
 * to the native app math package.
 */
struct QdrSceneViewportCameraVector final {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

/**
 * Stores the renderer-neutral viewport camera pose persisted with one QDR
 * scene. Projection and lens settings remain owned by the viewport and scene
 * settings; only the editor camera transform is durable here.
 */
struct QdrSceneViewportCameraPose final {
  QdrSceneViewportCameraVector eye;
  QdrSceneViewportCameraVector target;
  QdrSceneViewportCameraVector up;
};

/**
 * Validates a persisted viewport camera basis without normalizing or repairing
 * malformed file data.
 */
[[nodiscard]] inline bool qdr_scene_viewport_camera_pose_is_valid(
    const QdrSceneViewportCameraPose &pose) noexcept {
  return camera_pose_basis_is_valid(pose.eye, pose.target, pose.up);
}

} // namespace quader
