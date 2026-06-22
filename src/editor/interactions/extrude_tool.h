////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>

#include <string>
#include <vector>

namespace quader_editor_core {

/**
 * Enumerates ExtrudePreviewMode values used by the modeling layer.
 */
enum class ExtrudePreviewMode {
	zero,
	positive,
	negative,
};

/**
 * Stores the Extrude Tool State data contract used by the modal modeling interaction layer.
 */
struct ExtrudeToolState {
	bool active = false;
	int source_object_id = -1;
	int source_face_index = -1;
	quader_poly::Vec3 face_normal;
	std::vector<quader_poly::Vec3> face_vertices;
	std::vector<int> face_vertex_indices;
	quader_poly::Vec3 face_center;
	quader_poly::Vec2 screen_axis { 0.0F, 1.0F };
	double drag_distance = 0.0;
	double drag_pixels = 0.0;
	double preview_distance = 0.0;
	int requested_cut_count = 0;
	int active_cut_count = 0;
	int max_cut_count = 0;
	bool precision_mode = false;
	bool preview_valid = false;
};

/**
 * Represents an Extrude Begin Input value used by the modal modeling interaction layer.
 */
struct ExtrudeBeginInput {
	int source_object_id = -1;
	int source_face_index = -1;
	quader_poly::Vec3 face_normal;
	std::vector<quader_poly::Vec3> face_vertices;
	std::vector<int> face_vertex_indices;
	quader_poly::Vec3 face_center;
	quader_poly::Vec2 screen_axis { 0.0F, 1.0F };
};

/**
 * Stores the Extrude State Result data contract used by the modal modeling interaction layer.
 */
struct ExtrudeStateResult {
	bool ok = false;
	bool changed = false;
	std::string message;
	ExtrudeToolState state;
};

using ExtrudeBeginResult = ExtrudeStateResult;
using ExtrudeCommitResult = ExtrudeStateResult;

/**
 * Implements the Extrude Preview Operation modeling operation data used by the modal modeling interaction layer.
 */
struct ExtrudePreviewOperation {
	bool ok = false;
	std::string message;
	double preview_distance = 0.0;
	int active_cut_count = 0;
	int max_cut_count = 0;
	int requested_cut_count = 0;
	ExtrudePreviewMode preview_mode = ExtrudePreviewMode::zero;
	bool select_source = true;
	bool clear_preview_meshes = true;
};

/**
 * Represents an Extrude Preview Status value used by the modal modeling interaction layer.
 */
struct ExtrudePreviewStatus {
	bool ok = false;
	std::string status_action;
	double status_distance = 0.0;
	bool show_cut_status = false;
	int active_cut_count = 0;
	int max_cut_count = 0;
	bool grid_enabled = false;
	bool preview_valid = false;
};

ExtrudeStateResult begin_extrude_drag(const ExtrudeBeginInput& input);
double snap_extrude_distance(double distance, bool precision, bool grid_enabled, double grid_size);
double extrude_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size);
ExtrudeStateResult set_extrude_preview_distance(ExtrudeToolState& state, double distance, bool grid_enabled, double grid_size);
ExtrudeStateResult preview_extrude_from_distance(ExtrudeToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
ExtrudeStateResult preview_extrude_from_drag_pixels(ExtrudeToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
ExtrudeStateResult adjust_extrude_cut_count(ExtrudeToolState& state, int delta, bool grid_enabled, double grid_size);
double extrude_axis_distance_from_ray(const ExtrudeToolState& state, const quader_poly::Transform3& model_transform, const quader_poly::Ray& world_ray);
quader_poly::Vec3 extrude_face_center_for_vertices(const std::vector<quader_poly::Vec3>& vertices);
void set_extrude_preview_valid(ExtrudeToolState& state, bool valid);
ExtrudePreviewOperation extrude_preview_operation(const ExtrudeToolState& state);
ExtrudePreviewStatus extrude_preview_status(const ExtrudeToolState& state, bool grid_enabled);
void reset_extrude(ExtrudeToolState& state);

} // namespace quader_editor_core
