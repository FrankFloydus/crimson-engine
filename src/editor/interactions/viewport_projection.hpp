////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/tool_constants.hpp>
#include <mesh/polygon/document.hpp>

#include <string>

namespace quader_editor_core {

/**
 * Enumerates CameraProjection values used by the modeling layer.
 */
enum class CameraProjection {
  Perspective,
  Orthographic,
};

/**
 * Stores the Viewport Camera State data contract used by the modal modeling interaction layer.
 */
struct ViewportCameraState {
  CameraProjection projection = CameraProjection::Perspective;
  quader_poly::Transform3 transform;
  quader::QVec2 viewport_size{1.0F, 1.0F};
  double orthographic_size = 1.0;
  double fov_degrees = kNavigationDefaultFovDegrees;
};

/**
 * Stores the Cut Grid Point Result data contract used by the modal modeling interaction layer.
 */
struct CutGridPointResult {
	bool ok = false;
	std::string message;
	quader::QVec3 position;
	quader::QVec3 plane_normal { 0.0F, 1.0F, 0.0F };
};

bool project_viewport_point(const ViewportCameraState& camera, quader::QVec3 point, quader::QVec2& out_screen);
bool viewport_point_is_behind_camera(const ViewportCameraState& camera, quader::QVec3 point);
quader_poly::Ray ray_from_viewport_screen_point(const ViewportCameraState& camera, quader::QVec2 screen);
quader::QVec3 cut_grid_plane_normal_for_camera(const ViewportCameraState& camera);
CutGridPointResult viewport_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size, bool snap_enabled);
CutGridPointResult cut_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size);

} // namespace quader_editor_core
