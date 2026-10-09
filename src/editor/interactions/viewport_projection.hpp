////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/tool_constants.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

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
  bool affine_model_space = false;
  quader_poly::Transform3 transform;
  quader::QVec2 viewport_size{1.0F, 1.0F};
  double orthographic_size = 1.0;
  double fov_degrees = kNavigationDefaultFovDegrees;
};

/**
 * Immutable camera-only arithmetic shared by every point and ray in one query.
 * Retains the scalar projection's float dot products and division order.
 */
class PreparedViewportProjection final {
public:
  explicit PreparedViewportProjection(const ViewportCameraState &camera);

  [[nodiscard]] bool project_point(quader::QVec3 point,
                                   quader::QVec2 &out_screen) const;
  [[nodiscard]] bool point_is_behind_camera(quader::QVec3 point) const;
  [[nodiscard]] quader_poly::Ray
  ray_from_screen_point(quader::QVec2 screen) const;

private:
  [[nodiscard]] quader::QVec3
  vector_from_camera_components(quader::QVec3 components) const;

  CameraProjection projection_;
  bool affine_model_space_;
  quader::QVec3 origin_;
  quader::QVec3 right_;
  quader::QVec3 up_;
  quader::QVec3 forward_;
  double width_;
  double height_;
  double aspect_;
  double half_height_ = 0.0;
  double tangent_ = 0.0;
  quader::QVec3 up_cross_forward_;
  quader::QVec3 forward_cross_right_;
  quader::QVec3 right_cross_up_;
  float determinant_ = 0.0F;
  bool affine_basis_valid_ = false;
  quader::QVec3 orthographic_affine_direction_;
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
CutGridPointResult cut_support_plane_point_from_ray(quader_poly::Ray ray, quader::QVec3 plane_origin, quader::QVec3 plane_normal, double grid_size);

} // namespace quader_editor_core
