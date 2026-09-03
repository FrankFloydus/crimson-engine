////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#ifndef QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
#define QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H

#include <editor/interactions/tool_identity.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_picking.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader_editor_core {

struct PolygonObject;
enum class SelectionMode;
struct PickObjectBounds;
struct PickObjectFaceGeometry;
struct PickObjectTransform;
struct PickAllWithSurfaceScratch;

/**
 * Stores the Move Tool State data contract used by the modal modeling interaction layer.
 */
struct MoveToolState {
	bool active = false;
        TransformAxis axis = TransformAxis::None;
        quader_poly::Transform3 orientation;
	quader_poly::Transform3 model_transform;
        quader::QVec3 world_axis;
	quader::QVec3 local_axis;
	quader::QVec3 center_local;
	quader::QVec3 center_world;
	quader::QVec2 screen_axis { 1.0F, 0.0F };
	quader::QVec2 start_screen;
	quader::QVec3 world_plane_normal;
	quader::QVec3 plane_start_point;
	bool has_axis_start_distance = false;
	double axis_start_distance = 0.0;
	double preview_distance = 0.0;
	quader::QVec3 preview_delta;
};

/**
 * Represents a Move Begin Input value used by the modal modeling interaction layer.
 */
struct MoveBeginInput {
  TransformAxis axis = TransformAxis::None;
  quader_poly::Transform3 orientation;
  bool has_world_direction = false;
  quader::QVec3 world_direction;
  quader_poly::Transform3 model_transform;
  quader::QVec3 center_local;
  bool has_world_center = false;
  quader::QVec3 world_center;
  bool has_screen_axis = false;
  quader::QVec2 screen_pivot;
  quader::QVec2 screen_tip;
  quader::QVec2 screen_position;
  bool has_start_ray = false;
  quader_poly::Ray start_ray;
  bool has_camera = false;
  quader_poly::Transform3 camera_transform;
};

/**
 * Stores the Move Begin Result data contract used by the modal modeling interaction layer.
 */
struct MoveBeginResult {
	bool ok = false;
	std::string message;
	MoveToolState state;
};

/**
 * Stores the Move Preview Result data contract used by the modal modeling interaction layer.
 */
struct MovePreviewResult {
	bool ok = false;
	std::string message;
	double distance = 0.0;
	double preview_distance = 0.0;
	quader::QVec3 preview_delta;
};

/// One stationary mesh landmark resolved by the canonical Move snap picker.
struct MoveVertexSnapTarget {
  int object_id = -1;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  quader::QVec3 position = {};
  double ray_distance = 0.0;
  double pick_distance = 0.0;
};

/// One prepared Move snap query. Surface depth is produced by the same
/// component-picking traversal used to resolve the mesh vertex.
struct MoveVertexSnapQueryResult {
  std::optional<MoveVertexSnapTarget> vertex_target;
  std::optional<double> nearest_surface_ray_distance;
};

MoveBeginResult begin_move_drag(const MoveBeginInput& input);
[[nodiscard]] bool move_axis_supports_vertex_snap(TransformAxis axis);
[[nodiscard]] quader_poly::PickOptions
move_vertex_snap_pick_options(double vertex_radius);
[[nodiscard]] bool move_selection_contains_vertex(
    const PolygonObject& object, SelectionMode selection_mode,
    quader_poly::ElementId vertex_id);
[[nodiscard]] std::optional<MoveVertexSnapTarget>
resolve_move_vertex_snap_target(
    const std::vector<PolygonObject>& objects, SelectionMode selection_mode,
    const quader_poly::Ray& ray, double vertex_radius,
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {},
    bool exclude_current_selection = true);
/// Resolves a mesh-only candidate beneath an inclusive caller-owned
/// render-surface cutoff without traversing authored mesh faces. This is the
/// cross-domain viewport seam; ordinary modeling Move keeps using the complete
/// `resolve_move_vertex_snap_target` policy above.
[[nodiscard]] std::optional<MoveVertexSnapTarget>
resolve_move_vertex_snap_candidate(
    const std::vector<PolygonObject>& objects, SelectionMode selection_mode,
    const quader_poly::Ray& ray, double vertex_radius,
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectTransform> object_transforms = {},
    bool exclude_current_selection = true,
    std::optional<double> maximum_world_ray_distance = std::nullopt,
    PickAllWithSurfaceScratch *picking_scratch = nullptr);
[[nodiscard]] MoveVertexSnapQueryResult resolve_move_vertex_snap_query(
    const std::vector<PolygonObject>& objects, SelectionMode selection_mode,
    const quader_poly::Ray& ray, double vertex_radius,
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {},
    bool exclude_current_selection = true,
    PickAllWithSurfaceScratch *picking_scratch = nullptr);
double move_axis_distance(const MoveToolState& state, const quader_poly::Ray& ray);
MovePreviewResult preview_move_from_distance(MoveToolState& state, double distance, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_from_drag_pixels(MoveToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size);
MovePreviewResult preview_move_to_axis_snap_target(MoveToolState& state, quader::QVec3 target_position);
MovePreviewResult preview_move_from_viewport(
	MoveToolState& state,
	quader::QVec2 screen_position,
	quader_poly::Ray ray,
	const ViewportCameraState& camera,
	bool precision,
	bool grid_enabled,
	double grid_size,
	bool snap_center_to_grid = false);
void reset_move(MoveToolState& state);

} // namespace quader_editor_core

#endif // QUADER_EDITOR_CORE_TOOL_MOVE_TOOL_H
