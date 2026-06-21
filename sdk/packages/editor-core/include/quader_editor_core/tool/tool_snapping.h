#pragma once

#include <quader_poly/document.h>

#include <optional>

namespace quader_editor_core {

double snap_world_value(double value, double grid_size);
double maybe_snap_world_value(double value, double grid_size, bool grid_enabled);
float snap_insert_edge_loop_factor(
	const quader_poly::Document& document,
	quader_poly::Edge edge,
	float factor,
	double grid_size,
	bool snap_enabled);
std::optional<float> snap_insert_edge_loop_preview_factor(
	const quader_poly::Document& document,
	quader_poly::Edge edge,
	float factor,
	double grid_size,
	bool snap_enabled);
quader_poly::Vec3 snap_knife_point_to_grid(quader_poly::Vec3 point, double grid_size, bool snap_enabled);
quader_poly::Vec3 constrain_knife_point_to_axis(
	quader_poly::Vec3 previous,
	quader_poly::Vec3 point,
	quader_poly::Vec3 face_normal,
	double grid_size,
	bool snap_enabled);

} // namespace quader_editor_core
