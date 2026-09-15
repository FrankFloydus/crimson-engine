////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_context_menu_policy.hpp"

#include "api/quader_api_types.hpp"
#include "api/quader_command_ids.hpp"

namespace quader {
namespace {
constexpr unsigned kObject = 1U;
constexpr unsigned kGroup = 2U;
constexpr unsigned kVertex = 4U;
constexpr unsigned kEdge = 8U;
constexpr unsigned kFace = 16U;

enum class Operand { Selection, Polygon, Groupable, Group, Clipboard };

struct CommandScope {
  QuaderCommandId command;
  unsigned modes;
  Operand operand;
};

// Explicit operand scope is independent of command activation and presentation
// categories. New catalog entries must choose their RMB placement deliberately.
// Viewport tool launchers belong to the toolbar, not the operations menu.
// Bridge uses the editable preview once, rather than two identical labels.
constexpr CommandScope kCommandScopes[]{
    {quader_command::kDuplicateSelection, kObject | kGroup, Operand::Selection},
    {quader_command::kEditCreateInstance, kObject | kGroup, Operand::Selection},
    {quader_command::kEditFlattenInstance, kObject | kGroup, Operand::Selection},
    {quader_command::kEditExitInstanceEdit, kObject | kGroup, Operand::Selection},
    {quader_command::kGroupSelection, kObject | kGroup, Operand::Groupable},
    {quader_command::kUngroupSelection, kObject | kGroup, Operand::Group},
    {quader_command::kCutSelection, kObject, Operand::Clipboard},
    {quader_command::kCopySelection, kObject, Operand::Clipboard},
    {quader_command::kPasteSelection, kObject, Operand::Clipboard},
    {quader_command::kModelingMirrorSelectionX, kObject, Operand::Polygon},
    {quader_command::kModelingMirrorSelectionY, kObject, Operand::Polygon},
    {quader_command::kModelingMirrorSelectionZ, kObject, Operand::Polygon},
    {quader_command::kModelingFlipHorizontal, kObject, Operand::Polygon},
    {quader_command::kModelingFlipVertical, kObject, Operand::Polygon},
    {quader_command::kModelingCopyPivotFromActive, kObject, Operand::Polygon},
    {quader_command::kModelingSnapMeshesToFloor, kObject, Operand::Polygon},
    {quader_command::kModelingDuplicateMeshes, kObject, Operand::Polygon},
    {quader_command::kModelingCombineMesh, kObject, Operand::Polygon},
    {quader_command::kModelingCreateOuterHull, kObject, Operand::Polygon},
    {quader_command::kModelingHalfCut, kObject, Operand::Polygon},
    {quader_command::kModelingInvertMesh, kObject, Operand::Polygon},
    {quader_command::kModelingShadeSmoothMesh, kObject, Operand::Polygon},
    {quader_command::kModelingShadeFlatMesh, kObject, Operand::Polygon},
    {quader_command::kModelingRemoveDoubles, kObject, Operand::Polygon},
    {quader_command::kModelingAlignXMin, kObject, Operand::Polygon},
    {quader_command::kModelingAlignXCenter, kObject, Operand::Polygon},
    {quader_command::kModelingAlignXMax, kObject, Operand::Polygon},
    {quader_command::kModelingAlignYMin, kObject, Operand::Polygon},
    {quader_command::kModelingAlignYCenter, kObject, Operand::Polygon},
    {quader_command::kModelingAlignYMax, kObject, Operand::Polygon},
    {quader_command::kModelingAlignZMin, kObject, Operand::Polygon},
    {quader_command::kModelingAlignZCenter, kObject, Operand::Polygon},
    {quader_command::kModelingAlignZMax, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeCentersX, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeCentersY, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeCentersZ, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeGapsX, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeGapsY, kObject, Operand::Polygon},
    {quader_command::kModelingDistributeGapsZ, kObject, Operand::Polygon},
    {quader_command::kModelingRecalculateFaceNormals, kObject | kFace, Operand::Polygon},
    {quader_command::kModelingConnectVertices, kVertex, Operand::Selection},
    {quader_command::kModelingBevelVertices, kVertex, Operand::Selection},
    {quader_command::kModelingSnapVerticesToActive, kVertex, Operand::Selection},
    {quader_command::kModelingSnapVerticesToFloor, kVertex, Operand::Selection},
    {quader_command::kModelingRadialAlignVertices, kVertex, Operand::Selection},
    {quader_command::kModelingMergeVertices, kVertex, Operand::Selection},
    {quader_command::kModelingMergeVerticesAtCenter, kVertex, Operand::Selection},
    {quader_command::kModelingMergeByDistance, kVertex, Operand::Selection},
    {quader_command::kModelingDissolveVertices, kVertex, Operand::Selection},
    {quader_command::kModelingConnectEdges, kEdge, Operand::Selection},
    {quader_command::kModelingSplitEdges, kEdge, Operand::Selection},
    {quader_command::kModelingFillHole, kEdge, Operand::Selection},
    {quader_command::kModelingInterpolatedBridgeEdges, kEdge, Operand::Selection},
    {quader_command::kModelingBevelEdges, kEdge, Operand::Selection},
    {quader_command::kModelingSnapEdgesToActive, kEdge, Operand::Selection},
    {quader_command::kModelingRadialAlignEdges, kEdge, Operand::Selection},
    {quader_command::kModelingMergeEdges, kEdge, Operand::Selection},
    {quader_command::kModelingDefaultEdgeNormals, kEdge, Operand::Selection},
    {quader_command::kModelingHardenEdgeNormals, kEdge, Operand::Selection},
    {quader_command::kModelingSoftenEdgeNormals, kEdge, Operand::Selection},
    {quader_command::kModelingDissolveEdges, kEdge, Operand::Selection},
    {quader_command::kModelingCollapseEdges, kEdge, Operand::Selection},
    {quader_command::kModelingExtractFaces, kFace, Operand::Selection},
    {quader_command::kModelingDetachFaces, kFace, Operand::Selection},
    {quader_command::kModelingCombineFaces, kFace, Operand::Selection},
    {quader_command::kModelingInterpolatedBridgeFaces, kFace, Operand::Selection},
    {quader_command::kModelingSliceQuad, kFace, Operand::Selection},
    {quader_command::kModelingThickenFaces, kFace, Operand::Selection},
    {quader_command::kModelingRadialAlignFaces, kFace, Operand::Selection},
    {quader_command::kModelingInvertFaces, kFace, Operand::Selection},
    {quader_command::kModelingShadeFacesSmooth, kFace, Operand::Selection},
    {quader_command::kModelingShadeFacesFlat, kFace, Operand::Selection},
    {quader_command::kModelingCollapseFaces, kFace, Operand::Selection},
};

unsigned mode_bit(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Object: return kObject;
  case QuaderSelectionMode::Group: return kGroup;
  case QuaderSelectionMode::Vertex: return kVertex;
  case QuaderSelectionMode::Edge: return kEdge;
  case QuaderSelectionMode::Face: return kFace;
  }
  return 0U;
}
} // namespace

bool viewport_context_menu_allows_command(
    const ViewportContextMenuScope &scope, std::string_view command_id) {
  if (command_id == quader_command::kDeleteSelection.string()) {
    return scope.has_selection;
  }
  if (scope.curve_controls_selected) {
    return false;
  }
  for (const auto &entry : kCommandScopes) {
    if (entry.command.string() != command_id) {
      continue;
    }
    if ((entry.modes & mode_bit(scope.mode)) == 0U) {
      return false;
    }
    switch (entry.operand) {
    case Operand::Selection: return scope.has_selection;
    case Operand::Polygon: return scope.polygon_targets;
    case Operand::Groupable: return scope.groupable_targets;
    case Operand::Group: return scope.has_group_target;
    case Operand::Clipboard:
      return scope.clipboard_targets ||
             (!scope.has_selection &&
              command_id == quader_command::kPasteSelection.string());
    }
  }
  return false;
}


bool is_viewport_context_menu_click_gesture(
    const ViewportContextMenuOpenRequest &request) {
  if (request.modal_tool_owns_viewport || !request.right_press_tracked ||
      !request.right_released || !request.release_inside_viewport) {
    return false;
  }

  const QVec2 delta = request.release_screen - request.press_screen;
  const float release_distance_squared = delta.x * delta.x + delta.y * delta.y;
  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return release_distance_squared <= request.max_click_distance_squared &&
         drag_distance_squared <= request.max_click_distance_squared;
}

bool viewport_context_menu_has_available_actions(
    bool has_modeling_actions, bool has_object_actions, bool has_selection,
    bool has_material_under_mouse, bool has_fixed_command_actions) {
  return has_modeling_actions || has_object_actions || has_selection ||
         has_material_under_mouse || has_fixed_command_actions;
}

std::optional<ViewportContextMenuModelingSection>
viewport_context_menu_modeling_section(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Object:
    return ViewportContextMenuModelingSection::Mesh;
  case QuaderSelectionMode::Vertex:
    return ViewportContextMenuModelingSection::Vertex;
  case QuaderSelectionMode::Edge:
    return ViewportContextMenuModelingSection::Edge;
  case QuaderSelectionMode::Face:
    return ViewportContextMenuModelingSection::Face;
  case QuaderSelectionMode::Group:
    return std::nullopt;
  }
  return std::nullopt;
}

ViewportContextMenuActionBucket
viewport_context_menu_action_bucket(QuaderSelectionMode mode,
                                    std::string_view action_id) {
  if (action_id == "copy_pivot_from_active") {
    return mode == QuaderSelectionMode::Object
               ? ViewportContextMenuActionBucket::Object
               : ViewportContextMenuActionBucket::Suppressed;
  }
  if (action_id == "delete_selection" && (mode == QuaderSelectionMode::Object ||
                                          mode == QuaderSelectionMode::Group)) {
    return ViewportContextMenuActionBucket::Object;
  }
  return ViewportContextMenuActionBucket::Modeling;
}

ViewportContextMenuActionDisposition
viewport_context_menu_action_disposition(QuaderSelectionMode mode,
                                         std::string_view action_id) {
  if (viewport_context_menu_action_bucket(mode, action_id) ==
      ViewportContextMenuActionBucket::Suppressed) {
    return ViewportContextMenuActionDisposition::Suppressed;
  }
  if (mode == QuaderSelectionMode::Group &&
      action_id == "duplicate_meshes") {
    return ViewportContextMenuActionDisposition::Suppressed;
  }

  switch (mode) {
  case QuaderSelectionMode::Object:
    if (action_id == "snap_meshes_to_floor" || action_id == "combine_mesh" ||
        action_id == "half_cut" || action_id == "create_outer_hull") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Vertex:
    if (action_id == "merge_vertices_at_center" ||
        action_id == "bevel_vertices" || action_id == "connect_vertices") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Edge:
    if (action_id == "bevel_edges" || action_id == "bridge_edges" ||
        action_id == "interpolated_bridge_edges" ||
        action_id == "connect_edges" || action_id == "fill_hole") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Face:
    if (action_id == "thicken_faces" || action_id == "bridge_faces" ||
        action_id == "interpolated_bridge_faces" ||
        action_id == "combine_faces" || action_id == "extract_faces") {
      return ViewportContextMenuActionDisposition::Primary;
    }
    break;
  case QuaderSelectionMode::Group:
    break;
  }

  return ViewportContextMenuActionDisposition::Catalog;
}

std::uint32_t viewport_context_menu_primary_rank(QuaderSelectionMode mode,
                                                 std::string_view action_id) {
  switch (mode) {
  case QuaderSelectionMode::Object:
    if (action_id == "snap_meshes_to_floor") {
      return 10U;
    }
    if (action_id == "combine_mesh") {
      return 20U;
    }
    if (action_id == "half_cut") {
      return 30U;
    }
    if (action_id == "create_outer_hull") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Vertex:
    if (action_id == "merge_vertices_at_center") {
      return 10U;
    }
    if (action_id == "bevel_vertices") {
      return 20U;
    }
    if (action_id == "connect_vertices") {
      return 30U;
    }
    break;
  case QuaderSelectionMode::Edge:
    if (action_id == "bevel_edges") {
      return 10U;
    }
    if (action_id == "bridge_edges" || action_id == "interpolated_bridge_edges") {
      return 20U;
    }
    if (action_id == "connect_edges") {
      return 30U;
    }
    if (action_id == "fill_hole") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Face:
    if (action_id == "thicken_faces") {
      return 10U;
    }
    if (action_id == "bridge_faces" || action_id == "interpolated_bridge_faces") {
      return 20U;
    }
    if (action_id == "combine_faces") {
      return 30U;
    }
    if (action_id == "extract_faces") {
      return 40U;
    }
    break;
  case QuaderSelectionMode::Group:
    break;
  }

  return 500U;
}

bool should_open_viewport_context_menu(
    const ViewportContextMenuOpenRequest &request) {
  return is_viewport_context_menu_click_gesture(request) &&
         (request.target_hit || request.has_target_independent_actions) &&
         request.has_actions;
}

bool should_preserve_viewport_context_menu_hover(
    const ViewportContextMenuHoverRequest &request) {
  if (!request.right_press_tracked || request.fly_hud_active) {
    return false;
  }

  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return drag_distance_squared <= request.max_click_distance_squared;
}

} // namespace quader
