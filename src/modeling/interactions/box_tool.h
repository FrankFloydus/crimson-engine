////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/runtime/session.h>

#include <limits>
#include <string>
#include <vector>

namespace quader_editor_core {

/**
 * Enumerates BoxDragState values used by the modeling layer.
 */
enum class BoxDragState {
	idle,
	footprint,
	height,
};

/**
 * Represents a Box Construction Plane value used by the modal modeling interaction layer.
 */
struct BoxConstructionPlane {
	quader_poly::Vec3 origin;
	quader_poly::Vec3 snap_origin;
	quader_poly::Vec3 normal { 0.0F, 1.0F, 0.0F };
	quader_poly::Vec3 axis_u { 1.0F, 0.0F, 0.0F };
	quader_poly::Vec3 axis_v { 0.0F, 0.0F, 1.0F };
};

/**
 * Stores the Box Tool State data contract used by the modal modeling interaction layer.
 */
struct BoxToolState {
	BoxDragState drag_state = BoxDragState::idle;
	BoxConstructionPlane plane;
	quader_poly::Vec3 raw_start_point;
	quader_poly::Vec3 raw_end_point;
	quader_poly::Vec3 start_point;
	quader_poly::Vec3 end_point;
	double height_distance = 0.0;
	quader_poly::Vec2 height_mouse_start;
	double height_start_distance = 0.0;
	double height_axis_start = std::numeric_limits<double>::infinity();
	int preview_brush_id = -1;
	bool preview_valid = false;
	std::vector<quader_poly::Vec3> hover_cell_points;
};

/**
 * Stores the Box Tool State Result data contract used by the modal modeling interaction layer.
 */
struct BoxToolStateResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	BoxToolState state;
};

/**
 * Stores the Box Construction Point Result data contract used by the modal modeling interaction layer.
 */
struct BoxConstructionPointResult {
	bool ok = false;
	std::string message;
	quader_poly::Vec3 position;
	double distance = 0.0;
};

/**
 * Stores the Box Footprint Descriptor data contract used by the modal modeling interaction layer.
 */
struct BoxFootprintDescriptor {
	bool ok = false;
	std::string message;
	std::vector<quader_poly::Vec3> footprint;
	quader_poly::Vec2 size;
	quader_poly::Vec2 signed_size;
	quader_poly::Vec3 center;
};

/**
 * Stores the Box Preview Descriptor data contract used by the modal modeling interaction layer.
 */
struct BoxPreviewDescriptor {
	bool ok = false;
	std::string message;
	BoxDragState drag_state = BoxDragState::idle;
	BoxFootprintDescriptor footprint;
	quader_poly::Vec3 normal { 0.0F, 1.0F, 0.0F };
	double signed_height = 1.0;
	double distance = 1.0;
	Bounds3 bounds;
	int preview_brush_id = -1;
	bool preview_valid = false;
};

quader_poly::Vec3 project_box_point_to_plane(quader_poly::Vec3 point, quader_poly::Vec3 origin, quader_poly::Vec3 normal);
BoxConstructionPlane normalize_box_construction_plane(quader_poly::Vec3 origin, quader_poly::Vec3 normal);
BoxToolStateResult set_box_construction_plane(BoxToolState& state, quader_poly::Vec3 origin, quader_poly::Vec3 normal);
BoxConstructionPointResult box_construction_point_from_ray(const BoxConstructionPlane& plane, const quader_poly::Ray& ray);
quader_poly::Vec2 box_directional_snap_pair(double start_value, double end_value, double grid_size);
quader_poly::Vec2 box_footprint_signed_size(quader_poly::Vec3 start_point, quader_poly::Vec3 end_point, quader_poly::Vec3 axis_u, quader_poly::Vec3 axis_v, double grid_size);
std::vector<quader_poly::Vec3> box_footprint_points(quader_poly::Vec3 start_point, quader_poly::Vec3 end_point, quader_poly::Vec3 axis_u, quader_poly::Vec3 axis_v, double grid_size);
BoxFootprintDescriptor box_footprint_descriptor(quader_poly::Vec3 start_point, quader_poly::Vec3 end_point, quader_poly::Vec3 axis_u, quader_poly::Vec3 axis_v, double grid_size);
double snap_box_height_distance(double distance, bool grid_enabled, double grid_size);
BoxToolStateResult begin_box_footprint(BoxToolState& state, quader_poly::Vec3 raw_start, quader_poly::Vec3 raw_end, bool grid_enabled, double grid_size);
BoxToolStateResult update_box_footprint(BoxToolState& state, quader_poly::Vec3 raw_end, bool grid_enabled, double grid_size);
BoxToolStateResult update_box_hover_cell(BoxToolState& state, quader_poly::Vec3 local_point, bool grid_enabled, double grid_size);
BoxToolStateResult begin_box_height(BoxToolState& state, quader_poly::Vec2 screen_position, double axis_distance, bool has_axis_distance, bool grid_enabled, double grid_size);
double box_height_distance_for_ray(const BoxToolState& state, const quader_poly::Ray& ray, double grid_size);
BoxToolStateResult update_box_height_preview(BoxToolState& state, double axis_distance, bool has_axis_distance, quader_poly::Vec2 screen_position, bool grid_enabled, double grid_size);
BoxPreviewDescriptor box_preview_descriptor(const BoxToolState& state, bool grid_enabled, double grid_size);
std::vector<quader_poly::Vec3> box_preview_corners(const BoxPreviewDescriptor& preview);
void set_box_preview_state(BoxToolState& state, int preview_brush_id, bool preview_valid);
void clear_box_preview_state(BoxToolState& state);
void sync_box_state(BoxToolState& state, const BoxToolState& source);
void reset_box(BoxToolState& state);

} // namespace quader_editor_core
