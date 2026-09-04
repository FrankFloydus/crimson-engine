////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <math/types.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace quader_editor_core {

/**
 * Enumerates SelectionMode values used by the modeling layer.
 */
enum class SelectionMode {
  Vertex = 0,
  Edge = 1,
  Face = 2,
  Object = 3,
};

/**
 * Enumerates SelectionEditOperation values used by the modeling layer.
 */
enum class SelectionEditOperation {
  Replace = 0,
  Add = 1,
  Remove = 2,
  Toggle = 3,
};

/**
 * Enumerates ActiveSelectionKind values used by the modeling layer.
 */
enum class ActiveSelectionKind {
  None,
  Object,
  Vertex,
  Edge,
  Face,
};

/**
 * Enumerates SelectionHoverKind values used by the modeling layer.
 */
enum class SelectionHoverKind {
  None = 0,
  Object = 1,
  Vertex = 2,
  Edge = 3,
  Face = 4,
};

/**
 * Enumerates SelectionOverlayLayer values used by the modeling layer.
 */
enum class SelectionOverlayLayer {
  SelectedVisibleFace = 0,
  SelectedFaceEdge = 1,
  UnselectedVertex = 2,
  SelectedVertex = 3,
  HoverFace = 4,
  HoverFaceEdge = 5,
  HoverEdge = 6,
  HoverVertex = 7,
  SelectedEdge = 8,
  SelectedXrayFace = 9,
  SelectedFaceMask = 10,
  SourceWire = 11,
  OpenEdge = 12,
  SnapTargetVertex = 13,
  DiagnosticEdge = 14,
  SourceWireDepthStamp = 15,
};

/**
 * Enumerates semantic SelectionOverlayStyleRole values emitted by the modeling
 * runtime.
 */
enum class SelectionOverlayStyleRole : std::uint32_t {
  SourceWire = 1,
  SelectionFace = 2,
  SelectionWire = 3,
  Vertex = 4,
  SelectedVertex = 5,
  HoverFace = 6,
  HoverWire = 7,
  HoverVertex = 8,
  RemoveSelectionFace = 9,
  RemoveSelectionWire = 10,
  OpenEdge = 11,
  SnapTargetVertex = 12,
  DiagnosticEdge = 13,
  FacePlaneHoverFace = 14,
  FacePlaneHoverWire = 15,
};

inline constexpr std::uint32_t kSelectionOverlayStyleRoleTokenMask =
    0x51000000U;
inline constexpr std::uint32_t kSelectionOverlayStyleRoleTokenValueMask =
    0x000000ffU;

[[nodiscard]] inline constexpr std::uint32_t
selection_overlay_style_token(SelectionOverlayStyleRole role) {
  return kSelectionOverlayStyleRoleTokenMask |
         (static_cast<std::uint32_t>(role) &
          kSelectionOverlayStyleRoleTokenValueMask);
}

[[nodiscard]] inline constexpr bool
selection_overlay_style_token_is_semantic(std::uint32_t token) {
  return (token & 0xffffff00U) == kSelectionOverlayStyleRoleTokenMask;
}

[[nodiscard]] inline constexpr std::optional<SelectionOverlayStyleRole>
selection_overlay_style_role_from_token(std::uint32_t token) {
  if (!selection_overlay_style_token_is_semantic(token)) {
    return std::nullopt;
  }
  switch (static_cast<SelectionOverlayStyleRole>(
      token & kSelectionOverlayStyleRoleTokenValueMask)) {
  case SelectionOverlayStyleRole::SourceWire:
  case SelectionOverlayStyleRole::SelectionFace:
  case SelectionOverlayStyleRole::SelectionWire:
  case SelectionOverlayStyleRole::Vertex:
  case SelectionOverlayStyleRole::SelectedVertex:
  case SelectionOverlayStyleRole::HoverFace:
  case SelectionOverlayStyleRole::HoverWire:
  case SelectionOverlayStyleRole::HoverVertex:
  case SelectionOverlayStyleRole::RemoveSelectionFace:
  case SelectionOverlayStyleRole::RemoveSelectionWire:
  case SelectionOverlayStyleRole::OpenEdge:
  case SelectionOverlayStyleRole::SnapTargetVertex:
  case SelectionOverlayStyleRole::DiagnosticEdge:
  case SelectionOverlayStyleRole::FacePlaneHoverFace:
  case SelectionOverlayStyleRole::FacePlaneHoverWire:
    return static_cast<SelectionOverlayStyleRole>(
        token & kSelectionOverlayStyleRoleTokenValueMask);
  }
  return std::nullopt;
}

/**
 * Represents an Active Selection value used by the modeling runtime domain.
 */
struct ActiveSelection {
  bool has_active = false;
  ActiveSelectionKind kind = ActiveSelectionKind::None;
  SelectionMode selection_mode = SelectionMode::Object;
  int object_id = -1;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
};

/**
 * Stores the Selection Hover State data contract used by the modeling runtime
 * domain.
 */
struct SelectionHoverState {
  int object_id = -1;
  SelectionHoverKind kind = SelectionHoverKind::None;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  bool selected_modifier_pressed = false;
  bool face_plane_hover = false;
  int candidate_object_id = -1;
};

/**
 * Stores the Selection Hover Update Result data contract used by the modeling
 * runtime domain.
 */
struct SelectionHoverUpdateResult {
  bool changed = false;
  SelectionHoverState hover;
};

/**
 * Represents a Selection Overlay Triangle value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayTriangle {
  SelectionOverlayLayer layer = SelectionOverlayLayer::SelectedVisibleFace;
  quader::QVec3 a;
  quader::QVec3 b;
  quader::QVec3 c;
  std::uint32_t color_abgr = 0xffffffffU;
  int object_id = -1;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  quader_poly::ElementId vertex_a = quader_poly::kInvalidElementId;
  quader_poly::ElementId vertex_b = quader_poly::kInvalidElementId;
  quader_poly::ElementId vertex_c = quader_poly::kInvalidElementId;
};

/**
 * Represents a Selection Overlay Line value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayLine {
  SelectionOverlayLayer layer = SelectionOverlayLayer::SelectedFaceEdge;
  quader::QVec3 start;
  quader::QVec3 end;
  std::uint32_t color_abgr = 0xffffffffU;
  float width_pixels = 1.0F;
  int object_id = -1;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
};

/**
 * Represents a Selection Overlay Point value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayPoint {
  SelectionOverlayLayer layer = SelectionOverlayLayer::UnselectedVertex;
  quader::QVec3 position;
  std::uint32_t color_abgr = 0xffffffffU;
  float size_pixels = 8.0F;
  int object_id = -1;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
};

/**
 * Carries the local-space interior-side reference for the one hovered edge
 * dimension represented by a selection overlay frame.
 */
struct SelectionHoverEdgeDimensionContext {
  quader::QVec3 inside_reference;
  int object_id = -1;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
};

/**
 * Represents a Selection Overlay Style value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayStyle {
  std::uint32_t source_wire_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::SourceWire);
  std::uint32_t selection_face_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::SelectionFace);
  std::uint32_t selected_edge_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::SelectionWire);
  std::uint32_t handle_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::Vertex);
  std::uint32_t selected_handle_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::SelectedVertex);
  std::uint32_t hover_face_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::HoverFace);
  std::uint32_t hover_face_edge_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::HoverWire);
  std::uint32_t hover_edge_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::HoverWire);
  std::uint32_t hover_vertex_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::HoverVertex);
  std::uint32_t face_plane_hover_face_color_abgr =
      selection_overlay_style_token(
          SelectionOverlayStyleRole::FacePlaneHoverFace);
  std::uint32_t face_plane_hover_line_color_abgr =
      selection_overlay_style_token(
          SelectionOverlayStyleRole::FacePlaneHoverWire);
  std::uint32_t hover_selected_face_color_abgr = selection_overlay_style_token(
      SelectionOverlayStyleRole::RemoveSelectionFace);
  std::uint32_t hover_selected_line_color_abgr = selection_overlay_style_token(
      SelectionOverlayStyleRole::RemoveSelectionWire);
  std::uint32_t open_edge_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::OpenEdge);
  std::uint32_t snap_target_vertex_color_abgr = selection_overlay_style_token(
      SelectionOverlayStyleRole::SnapTargetVertex);
  std::uint32_t diagnostic_edge_color_abgr =
      selection_overlay_style_token(SelectionOverlayStyleRole::DiagnosticEdge);
  float edge_line_width_pixels = 3.0F;
  float single_edge_line_width_pixels = 3.0F;
  float open_edge_line_width_pixels = 2.0F;
  float diagnostic_edge_line_width_pixels = 4.0F;
  float vertex_point_size_pixels = 8.0F;
  float selected_hover_point_growth_pixels = 1.0F;
  float snap_target_point_size_pixels = 14.0F;
};

/**
 * Stores the Selection Overlay Frame data contract used by the modeling runtime
 * domain.
 */
struct SelectionOverlayFrame {
  std::vector<SelectionOverlayTriangle> triangles;
  std::vector<SelectionOverlayLine> lines;
  std::vector<SelectionOverlayPoint> points;
  std::optional<SelectionHoverEdgeDimensionContext>
      hover_edge_dimension_context;
  bool hover_target_selected = false;
};

} // namespace quader_editor_core

