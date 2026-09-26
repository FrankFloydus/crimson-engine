////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/// Active camera gesture; each mode replaces the resting navigation row.
enum class QuaderInputNavigation { None, Orbit, Pan, Fly, Zoom };
/// Accepted selection interaction, projected without interpreting raw keys again.
enum class QuaderInputSelectionGesture { None, Box, Lasso, Paint, EdgeLoop, EdgeRing, DepthCycle, ExpandComponent, EnterInstance };
/// Selection membership operation captured when the gesture started.
enum class QuaderInputSelectionOperation { Replace, Add, Remove };
/// Semantic gizmo handle used to filter axis, plane, and trackball actions.
enum class QuaderInputTransformHandle { None, Axis, Plane, Center, Trackball };
/// Transform operation captured at mouse-down, unaffected by later modifiers.
enum class QuaderInputTransformOperation {
  None, Move, Rotate, Scale, Duplicate, Extrude, Inset, Pivot,
};
/// Hovered primitive subdivision control already resolved by the tool owner.
enum class QuaderInputPrimitiveControl {
  None, SubdivisionsX, SubdivisionsY, SubdivisionsZ, Sides, Segments, Rings,
  SubdivisionLevel, MajorSegments, MinorSegments,
};
/// Eligible material transfer reserved by the viewport pointer owner.
enum class QuaderInputMaterialTransfer {
  None, SelectedFaceToFace, SelectedFaceToMesh, FaceToSelection, FaceToMesh,
  ContinueTexture,
};

/// Read-only projection of the owner currently receiving viewport input.
/// Mouse positions and geometry are deliberately excluded: unchanged semantic
/// frames compare cheaply without querying catalogs or allocating overlays.
struct QuaderOperationInputContext {
  bool viewport_available = true;
  bool orthographic = false;
  bool orbit_available = true;
  QuaderInputNavigation navigation = QuaderInputNavigation::None;
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool held_vertex_snap = false;
  bool orbit_snap_active = false;
  bool grid_snap_available = true;
  bool grid_snap_enabled = true;
  bool angle_snap_available = true;
  bool angle_snap_enabled = true;
  bool pointer_dragging = false;
  QuaderInputSelectionGesture selection_gesture = QuaderInputSelectionGesture::None;
  QuaderInputSelectionOperation selection_operation = QuaderInputSelectionOperation::Replace;
  bool selection_crossing = false;
  bool selection_toggle = false;
  QuaderInputTransformHandle transform_handle = QuaderInputTransformHandle::None;
  QuaderInputTransformOperation transform_operation = QuaderInputTransformOperation::None;
  bool can_duplicate = false;
  bool can_component_extrude = false;
  bool can_component_inset = false;
  bool can_vertex_snap = false;
  bool quick_pivot = false;
  bool pivot_handle_hovered = false;
  bool linear_array_endpoint_hovered = false;
  QuaderInputPrimitiveControl primitive_control = QuaderInputPrimitiveControl::None;
  bool curve_anchor_hovered = false;
  bool curve_tangent_hovered = false;
  bool curve_anchor_selection_only = false;
  bool path_source_hovered = false;
  bool plane_endpoint_hovered = false;
  bool plane_body_hovered = false;
  bool stroke_close_hovered = false;
  bool point_light_range_hovered = false;
  bool point_light_range_dragging = false;
  QuaderInputMaterialTransfer material_transfer = QuaderInputMaterialTransfer::None;

  bool operator==(const QuaderOperationInputContext &) const = default;
};

} // namespace quader
