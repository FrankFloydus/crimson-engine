#pragma once

#include <quader_editor_core/tool_constants.h>
#include <quader_poly/document.h>

#include <string>

namespace quader_editor_core {

enum class CameraProjection {
	perspective,
	orthographic,
};

struct ViewportCameraState {
	CameraProjection projection = CameraProjection::perspective;
	quader_poly::Transform3 transform;
	quader_poly::Vec2 viewport_size { 1.0F, 1.0F };
	double orthographic_size = 1.0;
	double fov_degrees = navigation_default_fov_degrees;
};

struct CutGridPointResult {
	bool ok = false;
	std::string message;
	quader_poly::Vec3 position;
	quader_poly::Vec3 plane_normal { 0.0F, 1.0F, 0.0F };
};

bool project_viewport_point(const ViewportCameraState& camera, quader_poly::Vec3 point, quader_poly::Vec2& out_screen);
bool viewport_point_is_behind_camera(const ViewportCameraState& camera, quader_poly::Vec3 point);
quader_poly::Ray ray_from_viewport_screen_point(const ViewportCameraState& camera, quader_poly::Vec2 screen);
quader_poly::Vec3 cut_grid_plane_normal_for_camera(const ViewportCameraState& camera);
CutGridPointResult viewport_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size, bool snap_enabled);
CutGridPointResult cut_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size);

} // namespace quader_editor_core
