////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>

namespace quader_editor_core {

inline constexpr double tool_epsilon = 0.000001;
inline constexpr double screen_epsilon = 0.0001;
inline constexpr double move_pixels_per_grid_step = 48.0;
inline constexpr double scale_pixels_per_factor = 96.0;
inline constexpr double min_scale_factor = 0.01;
inline constexpr double min_extrude_distance = 0.001;
inline constexpr double extrude_pixels_per_grid_step = 48.0;
inline constexpr int extrude_cap_face_index = 1;
inline constexpr double min_box_size = 0.001;
inline constexpr double box_height_pixels_per_grid_step = 48.0;
inline constexpr double navigation_min_camera_distance = 0.05;
inline constexpr double navigation_default_frame_distance = 8.0;
inline constexpr double navigation_zoom_step_factor = 1.18;
inline constexpr double navigation_min_orthographic_size = 0.01;
inline constexpr double navigation_max_orthographic_size = 4096.0;
inline constexpr double navigation_max_orthographic_zoom_out_size = 1200.0;
inline constexpr double navigation_default_fov_degrees = 70.0;
inline constexpr double navigation_orbit_radians_per_pixel = 0.006;
inline constexpr double navigation_fly_look_radians_per_pixel = 0.004;
inline constexpr double navigation_fly_base_speed = 6.0;
inline constexpr double navigation_fly_fast_multiplier = 4.0;
inline constexpr double navigation_fly_slow_multiplier = 0.25;
inline constexpr double navigation_fly_max_delta_seconds = 0.25;
inline constexpr double navigation_orthogonal_snap_dot = 0.965925826289068;
inline constexpr double view_cube_min_size = 48.0;
inline constexpr double view_cube_max_size = 128.0;
inline constexpr double view_cube_half_factor = 0.23;
inline constexpr double view_cube_axis_factor = 0.45;
inline constexpr double view_cube_face_visibility_epsilon = 0.0001;
inline constexpr double selection_box_default_drag_threshold = 4.0;
inline constexpr double transform_gizmo_tau = 6.28318530717958647692;
inline constexpr double transform_gizmo_axis_hide_score = 0.02;
inline constexpr double transform_gizmo_axis_full_score = 0.1;
inline constexpr double measurement_default_map_units_per_world_unit = 32.0;
inline constexpr float pick_cycle_default_max_screen_distance = 4.0F;
inline constexpr float default_map_units_per_world_unit = 32.0F;
inline constexpr float default_scene_footprint_map_units = 128.0F;
inline constexpr float default_scene_height_map_units = 32.0F;
inline constexpr std::size_t editor_history_default_max_depth = 128;

} // namespace quader_editor_core
