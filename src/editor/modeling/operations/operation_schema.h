////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace quader_editor_core {

/**
 * Enumerates EditorOperationId values used by the modeling layer.
 */
enum class EditorOperationId {
	bridge,
	edge_bevel,
	thicken,
	slice_quad,
	merge_by_distance,
	bevel_vertices,
	duplicate_meshes,
	connect_vertices,
	dissolve_vertices,
	connect_edges,
	split_edges,
	dissolve_edges,
	merge_edges,
	collapse_edges,
	fill_hole,
	combine_faces,
	collapse_faces,
	radial_align,
	flip_face_normals,
	shade_faces_smooth,
	shade_faces_flat,
	harden_edge_normals,
	soften_edge_normals,
	combine_meshes,
	extract_faces,
	detach_faces,
	snap_vertices_to_active,
	snap_edges_to_active,
	merge_vertices_to_active,
	merge_vertices_to_center,
	recalculate_face_normals,
	assign_material,
	delete_selection,
};

inline constexpr double default_command_grid_world_size = 1.0;

/**
 * Represents a Command Grid Fact value used by the modeling runtime domain.
 */
struct CommandGridFact {
	double value = 0.0;
	bool valid = false;
};

/**
 * Represents a Command Distance Fact value used by the modeling runtime domain.
 */
struct CommandDistanceFact {
	double value = 0.0;
	bool valid = false;
};

/**
 * Represents a Command Vec3 Fact value used by the modeling runtime domain.
 */
struct CommandVec3Fact {
	quader_poly::Vec3 value {};
	bool valid = false;
};

/**
 * Represents an Int Operation Option Schema value used by the modeling runtime domain.
 */
struct IntOperationOptionSchema {
	std::string_view id;
	std::string_view label;
	int default_value = 0;
	int minimum = 0;
	int maximum = 0;
	int step = 1;
	bool hard_limits = true;
};

/**
 * Represents a Float Operation Option Schema value used by the modeling runtime domain.
 */
struct FloatOperationOptionSchema {
	std::string_view id;
	std::string_view label;
	double default_value = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	double step = 0.01;
	bool hard_limits = true;
};

/**
 * Represents a Bool Operation Option Schema value used by the modeling runtime domain.
 */
struct BoolOperationOptionSchema {
	std::string_view id;
	std::string_view label;
	bool default_value = false;
	bool visible = true;
};

/**
 * Represents a Bridge Operation Schema value used by the modeling runtime domain.
 */
struct BridgeOperationSchema {
	EditorOperationId id = EditorOperationId::bridge;
	std::string_view operation_id = "bridge";
	std::string_view label = "Bridge";
	IntOperationOptionSchema steps;
};

/**
 * Represents an Edge Bevel Operation Schema value used by the modeling runtime domain.
 */
struct EdgeBevelOperationSchema {
	EditorOperationId id = EditorOperationId::edge_bevel;
	std::string_view operation_id = "edge_bevel";
	std::string_view label = "Edge Bevel";
	FloatOperationOptionSchema width;
	FloatOperationOptionSchema profile;
	IntOperationOptionSchema segments;
	quader_poly::BevelProfileType default_profile_type = quader_poly::BevelProfileType::offset;
};

/**
 * Represents a Thicken Operation Schema value used by the modeling runtime domain.
 */
struct ThickenOperationSchema {
	EditorOperationId id = EditorOperationId::thicken;
	std::string_view operation_id = "thicken";
	std::string_view label = "Thicken";
	FloatOperationOptionSchema thickness;
	BoolOperationOptionSchema from_center;
};

/**
 * Represents a Slice Quad Operation Schema value used by the modeling runtime domain.
 */
struct SliceQuadOperationSchema {
	EditorOperationId id = EditorOperationId::slice_quad;
	std::string_view operation_id = "slice_quad";
	std::string_view label = "Slice Quad";
	IntOperationOptionSchema x_slices;
	IntOperationOptionSchema y_slices;
};

/**
 * Represents a Merge By Distance Operation Schema value used by the modeling runtime domain.
 */
struct MergeByDistanceOperationSchema {
	EditorOperationId id = EditorOperationId::merge_by_distance;
	std::string_view operation_id = "merge_by_distance";
	std::string_view label = "Merge by Distance";
	FloatOperationOptionSchema tolerance;
};

/**
 * Represents a Bevel Vertices Operation Schema value used by the modeling runtime domain.
 */
struct BevelVerticesOperationSchema {
	EditorOperationId id = EditorOperationId::bevel_vertices;
	std::string_view operation_id = "bevel_vertices";
	std::string_view label = "Bevel Vertices";
};

/**
 * Represents a Duplicate Meshes Operation Schema value used by the modeling runtime domain.
 */
struct DuplicateMeshesOperationSchema {
	EditorOperationId id = EditorOperationId::duplicate_meshes;
	std::string_view operation_id = "duplicate_meshes";
	std::string_view label = "Duplicate Meshes";
};

/**
 * Represents a Bridge Operation Settings value used by the modeling runtime domain.
 */
struct BridgeOperationSettings {
	int steps = 1;
};

/**
 * Represents an Edge Bevel Operation Settings value used by the modeling runtime domain.
 */
struct EdgeBevelOperationSettings {
	double width = 1.0;
	double profile = 0.5;
	int profile_type = static_cast<int>(quader_poly::BevelProfileType::offset);
	int segments = 1;
};

/**
 * Represents a Thicken Operation Settings value used by the modeling runtime domain.
 */
struct ThickenOperationSettings {
	double thickness = 0.25;
	bool from_center = false;
};

/**
 * Represents a Slice Quad Operation Settings value used by the modeling runtime domain.
 */
struct SliceQuadOperationSettings {
	int x_slices = 1;
	int y_slices = 1;
};

/**
 * Represents a Merge By Distance Operation Settings value used by the modeling runtime domain.
 */
struct MergeByDistanceOperationSettings {
	double tolerance = 0.125;
};

/**
 * Represents a Bevel Vertices Operation Settings value used by the modeling runtime domain.
 */
struct BevelVerticesOperationSettings {
	CommandGridFact focused_grid;
	CommandDistanceFact explicit_distance;
};

/**
 * Represents a Duplicate Meshes Operation Settings value used by the modeling runtime domain.
 */
struct DuplicateMeshesOperationSettings {
	CommandGridFact focused_grid;
	CommandVec3Fact explicit_offset;
};

/**
 * Represents a Recalculate Face Normals Operation Settings value used by the modeling runtime domain.
 */
struct RecalculateFaceNormalsOperationSettings {
	bool outside = true;
};

/**
 * Represents an Assign Material Operation Settings value used by the modeling runtime domain.
 */
struct AssignMaterialOperationSettings {
	std::uint32_t material_slot = 0;
	bool explicit_target = false;
	int object_id = -1;
	quader_poly::ElementId face_id = quader_poly::invalid_element_id;
};

/**
 * Represents a Selection Operation Settings value used by the modeling runtime domain.
 */
struct SelectionOperationSettings {};

inline constexpr BridgeOperationSchema bridge_operation_schema()
{
	return {
		EditorOperationId::bridge,
		"bridge",
		"Bridge",
		{ "steps", "Steps", 1, 1, 64, 1, true },
	};
}

inline constexpr EdgeBevelOperationSchema edge_bevel_operation_schema()
{
	return {
		EditorOperationId::edge_bevel,
		"edge_bevel",
		"Edge Bevel",
		{ "width", "Width", 1.0, 0.0625, 16384.0, 1.0, true },
		{ "profile", "Profile", 0.5, 0.0, 1.0, 0.01, true },
		{ "segments", "Segments", 1, 1, 16, 1, true },
		quader_poly::BevelProfileType::offset,
	};
}

inline constexpr ThickenOperationSchema thicken_operation_schema()
{
	return {
		EditorOperationId::thicken,
		"thicken",
		"Thicken",
		{ "thickness", "Thickness", 0.25, -4.0, 4.0, 0.01, false },
		{ "from_center", "From Center", false, false },
	};
}

inline constexpr SliceQuadOperationSchema slice_quad_operation_schema()
{
	return {
		EditorOperationId::slice_quad,
		"slice_quad",
		"Slice Quad",
		{ "x_slices", "X", 1, 0, 64, 1, true },
		{ "y_slices", "Y", 1, 0, 64, 1, true },
	};
}

inline constexpr MergeByDistanceOperationSchema merge_by_distance_operation_schema()
{
	return {
		EditorOperationId::merge_by_distance,
		"merge_by_distance",
		"Merge by Distance",
		{ "tolerance", "Tolerance", 0.125, 0.0, 128.0, 0.001, true },
	};
}

inline constexpr BevelVerticesOperationSchema bevel_vertices_operation_schema()
{
	return {
		EditorOperationId::bevel_vertices,
		"bevel_vertices",
		"Bevel Vertices",
	};
}

inline constexpr DuplicateMeshesOperationSchema duplicate_meshes_operation_schema()
{
	return {
		EditorOperationId::duplicate_meshes,
		"duplicate_meshes",
		"Duplicate Meshes",
	};
}

inline int normalize_operation_int(const IntOperationOptionSchema& schema, int value)
{
	if (!schema.hard_limits) {
		return value;
	}
	return std::clamp(value, schema.minimum, schema.maximum);
}

inline double normalize_operation_float(const FloatOperationOptionSchema& schema, double value)
{
	if (!std::isfinite(value)) {
		return schema.default_value;
	}
	if (!schema.hard_limits) {
		return value;
	}
	return std::clamp(value, schema.minimum, schema.maximum);
}

inline double command_grid_world_size_or_default(CommandGridFact grid)
{
	return grid.valid && std::isfinite(grid.value) && grid.value > 0.0 ?
		grid.value :
		default_command_grid_world_size;
}

inline float bevel_vertices_distance_from_settings(BevelVerticesOperationSettings settings)
{
	if (settings.explicit_distance.valid && std::isfinite(settings.explicit_distance.value)) {
		return static_cast<float>(settings.explicit_distance.value);
	}
	return static_cast<float>(command_grid_world_size_or_default(settings.focused_grid));
}

inline quader_poly::Vec3 duplicate_meshes_offset_from_settings(DuplicateMeshesOperationSettings settings)
{
	if (settings.explicit_offset.valid &&
			std::isfinite(settings.explicit_offset.value.x) &&
			std::isfinite(settings.explicit_offset.value.y) &&
			std::isfinite(settings.explicit_offset.value.z)) {
		return settings.explicit_offset.value;
	}
	return {};
}

inline quader_poly::BevelProfileType sanitize_edge_bevel_profile_type(int profile_type)
{
	return profile_type == static_cast<int>(quader_poly::BevelProfileType::offset) ?
		quader_poly::BevelProfileType::offset :
		quader_poly::BevelProfileType::offset;
}

inline BridgeOperationSettings normalize_bridge_operation_settings(BridgeOperationSettings settings)
{
	settings.steps = normalize_operation_int(bridge_operation_schema().steps, settings.steps);
	return settings;
}

inline EdgeBevelOperationSettings normalize_edge_bevel_operation_settings(EdgeBevelOperationSettings settings)
{
	const EdgeBevelOperationSchema schema = edge_bevel_operation_schema();
	settings.width = normalize_operation_float(schema.width, settings.width);
	settings.profile = normalize_operation_float(schema.profile, settings.profile);
	settings.profile_type = static_cast<int>(sanitize_edge_bevel_profile_type(settings.profile_type));
	settings.segments = normalize_operation_int(schema.segments, settings.segments);
	return settings;
}

inline ThickenOperationSettings normalize_thicken_operation_settings(ThickenOperationSettings settings)
{
	settings.thickness = normalize_operation_float(thicken_operation_schema().thickness, settings.thickness);
	return settings;
}

inline SliceQuadOperationSettings normalize_slice_quad_operation_settings(SliceQuadOperationSettings settings)
{
	const SliceQuadOperationSchema schema = slice_quad_operation_schema();
	settings.x_slices = normalize_operation_int(schema.x_slices, settings.x_slices);
	settings.y_slices = normalize_operation_int(schema.y_slices, settings.y_slices);
	return settings;
}

inline MergeByDistanceOperationSettings normalize_merge_by_distance_operation_settings(MergeByDistanceOperationSettings settings)
{
	settings.tolerance = normalize_operation_float(merge_by_distance_operation_schema().tolerance, settings.tolerance);
	return settings;
}

inline quader_poly::EdgeBevelSettings to_edge_bevel_settings(EdgeBevelOperationSettings settings)
{
	settings = normalize_edge_bevel_operation_settings(settings);
	quader_poly::EdgeBevelSettings result;
	result.width = static_cast<float>(settings.width);
	result.profile = static_cast<float>(settings.profile);
	result.profile_type = sanitize_edge_bevel_profile_type(settings.profile_type);
	result.segments = settings.segments;
	return result;
}

} // namespace quader_editor_core
