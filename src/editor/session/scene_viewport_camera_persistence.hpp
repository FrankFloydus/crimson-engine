////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "io/qdr/qdr_scene_viewport_camera.hpp"

#include <optional>

namespace quader {

[[nodiscard]] inline QdrSceneViewportCameraPose
qdr_viewport_camera_pose_for_scene(const SceneSession &session) {
  const ViewportCameraPose pose = session.persisted_viewport_camera_pose();
  return {
      .eye = {pose.eye.x, pose.eye.y, pose.eye.z},
      .target = {pose.target.x, pose.target.y, pose.target.z},
      .up = {pose.up.x, pose.up.y, pose.up.z},
  };
}

inline void append_qdr_viewport_camera_pose(
    const SceneSession &session, QdrProjectWriteSnapshot &snapshot) {
  snapshot.viewport_camera_pose =
      qdr_viewport_camera_pose_for_scene(session);
}

[[nodiscard]] inline std::optional<ViewportCameraPose>
viewport_camera_pose_from_qdr(
    const std::optional<QdrSceneViewportCameraPose> &stored) {
  if (!stored.has_value() ||
      !qdr_scene_viewport_camera_pose_is_valid(*stored)) {
    return std::nullopt;
  }
  return ViewportCameraPose{
      .eye = {stored->eye.x, stored->eye.y, stored->eye.z},
      .target = {stored->target.x, stored->target.y, stored->target.z},
      .up = {stored->up.x, stored->up.y, stored->up.z},
  };
}

} // namespace quader
