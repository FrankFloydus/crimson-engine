#pragma once

#include <quader_editor_core/tool/tool_identity.h>
#include <quader_poly/document.h>

namespace quader_editor_core {

struct ToolBounds3 {
	bool has_bounds = false;
	quader_poly::Vec3 min;
	quader_poly::Vec3 max;
};

quader_poly::Vec3 axis_vector(TransformAxis axis);
quader_poly::Vec3 rotation_axis_vector(TransformAxis axis);
quader_poly::Vec3 axis_scale(TransformAxis axis, double factor);
quader_poly::Vec3 transform_vector(const quader_poly::Transform3& transform, quader_poly::Vec3 value);
quader_poly::Vec3 transform_point(const quader_poly::Transform3& transform, quader_poly::Vec3 point);
quader_poly::Vec3 inverse_transform_vector(const quader_poly::Transform3& transform, quader_poly::Vec3 value);
quader_poly::Vec3 inverse_transform_point(const quader_poly::Transform3& transform, quader_poly::Vec3 point);
quader_poly::Transform3 pivoted_transform(const quader_poly::Transform3& transform, quader_poly::Vec3 pivot);
quader_poly::Transform3 rotation_transform(TransformAxis axis, double angle_radians);
quader_poly::Transform3 scale_transform(quader_poly::Vec3 scale);
double scale_delta(quader_poly::Vec3 scale);
double wrap_radians(double radians);
double snap_move_distance(double distance, bool precision, bool grid_enabled, double grid_size);
double snapped_scale_factor(TransformAxis axis, double factor, const ToolBounds3& bounds, quader_poly::Vec3 pivot, bool grid_enabled, double grid_size);

} // namespace quader_editor_core
