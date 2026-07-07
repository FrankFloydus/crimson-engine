////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/tool_identity.hpp>
#include <editor/modeling/operations/operation_schema.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>
#include <mesh/polygon/document.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader_editor_core {

struct ViewportCameraState;
struct CutToolState;
struct MirrorToolState;
enum class ViewportFlipOrientation;
class EditorSession;

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
 * Enumerates LightType values used by the modeling layer.
 */
enum class LightType {
  Directional = 0,
  Point = 1,
  Spot = 2,
};

/**
 * Enumerates LightShadowMode values used by the modeling layer.
 */
enum class LightShadowMode {
  None = 0,
  Hard = 1,
  Soft = 2,
};

/**
 * Enumerates LightFloatProperty values used by the modeling layer.
 */
enum class LightFloatProperty {
  Intensity = 0,
  Range = 1,
  SpotAngle = 2,
  InnerSpotAngle = 3,
  ShadowStrength = 4,
  ShadowBias = 5,
  ShadowNormalBias = 6,
  ShadowNearPlane = 7,
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
    return static_cast<SelectionOverlayStyleRole>(
        token & kSelectionOverlayStyleRoleTokenValueMask);
  }
  return std::nullopt;
}

/**
 * Represents a Polygon Object value used by the modeling runtime domain.
 */
struct PolygonObject {
  int id = -1;
  int material_id = 0;
  bool selected = false;
  quader_poly::Document document;
  quader_poly::Selection selection;
};

/**
 * Represents an object-scoped affine transform in polygon-document local space.
 */
struct ObjectLocalTransform {
  int object_id = -1;
  quader_poly::Transform3 transform;
};

/**
 * Represents an object-scoped vector in polygon-document local space.
 */
struct ObjectLocalOffset {
  int object_id = -1;
  quader::QVec3 offset;
};

/**
 * Represents a Light Entity value used by the modeling runtime domain.
 */
struct LightEntity {
  int id = -1;
  std::string name;
  bool enabled = true;
  LightType type = LightType::Point;
  bool selected = false;
  quader::QVec3 position{0.0F, 3.0F, 0.0F};
  quader::QVec3 direction{0.0F, -1.0F, 0.0F};
  std::uint32_t color_abgr = 0xFFFFFFFFU;
  float intensity = 1.0F;
  LightShadowMode shadow_mode = LightShadowMode::Soft;
  float shadow_strength = 1.0F;
  float shadow_bias = 0.05F;
  float shadow_normal_bias = 0.4F;
  float shadow_near_plane = 0.2F;
  float range = 10.0F;
  float spot_angle = 30.0F;
  float inner_spot_angle = 0.0F;
};

/**
 * Represents a Pick Object Bounds value used by the modeling runtime domain.
 */
struct PickObjectBounds {
  int object_id = -1;
  bool empty = true;
  quader::QVec3 min;
  quader::QVec3 max;
};

/**
 * Represents a Pick Object Face Geometry value used by the modeling runtime
 * domain.
 */
struct PickObjectFaceGeometry {
  int object_id = -1;
  std::vector<quader_poly::PickFaceTriangle> triangles;
};

/**
 * Represents a Bounds3 value used by the modeling runtime domain.
 */
struct Bounds3 {
  bool empty = true;
  quader::QVec3 min;
  quader::QVec3 max;
};

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
  bool hover_target_selected = false;
};

/**
 * Represents an Insert Edge Loop Preview value used by the modeling runtime
 * domain.
 */
struct InsertEdgeLoopPreview {
  bool valid = false;
  int object_id = -1;
  quader_poly::Edge edge;
  float factor = 0.5F;
  std::vector<SelectionOverlayLine> lines;
  std::string message;
};

/**
 * Stores the Insert Edge Loop Tool State data contract used by the modeling
 * runtime domain.
 */
struct InsertEdgeLoopToolState {
  bool active = false;
  bool dragging = false;
  int object_id = -1;
  quader_poly::Edge edge;
  float factor = 0.5F;
  std::vector<SelectionOverlayLine> preview_lines;
};

/**
 * Enumerates KnifeToolStatus values used by the modeling layer.
 */
enum class KnifeToolStatus {
  Idle = 0,
  HoverMiss = 1,
  Preview = 2,
  PreviousSet = 3,
  Committed = 4,
  InvalidTarget = 5,
};

/**
 * Stores the Knife Tool State data contract used by the modeling runtime
 * domain.
 */
struct KnifeToolState {
  bool active = false;
  bool has_active_stroke = false;
  bool has_current = false;
  bool current_target_valid = false;
  int target_object_id = -1;
  quader_poly::KnifePointTarget current_target;
  quader::QVec3 current_point;
  quader::QVec3 face_normal{0.0F, 1.0F, 0.0F};
  std::uint32_t active_stroke_start = 0;
  std::uint32_t previous_point_index = 0;
  std::vector<quader_poly::KnifePointTarget> points;
  std::vector<quader::QVec3> point_positions;
  std::vector<quader_poly::KnifeStrokeSegment> segments;
  std::vector<quader::QVec3> intersection_preview_points;
  KnifeToolStatus status = KnifeToolStatus::Idle;
  std::vector<SelectionOverlayLine> preview_lines;
  std::vector<SelectionOverlayPoint> preview_points;
};

/**
 * Represents a Knife Tool Preview value used by the modeling runtime domain.
 */
struct KnifeToolPreview {
  bool ok = true;
  bool active = false;
  bool has_active_stroke = false;
  bool has_current = false;
  int target_object_id = -1;
  quader::QVec3 previous_point;
  quader::QVec3 current_point;
  std::uint32_t preview_point_count = 0;
  std::uint32_t preview_line_count = 0;
  std::uint32_t point_count = 0;
  std::uint32_t segment_count = 0;
  bool can_commit = false;
  KnifeToolStatus status = KnifeToolStatus::Idle;
};

/**
 * Represents a Knife Segment Preview value used by the modeling runtime domain.
 */
struct KnifeSegmentPreview {
  bool valid = false;
  int object_id = -1;
  quader_poly::KnifePointTarget previous;
  quader_poly::KnifePointTarget current;
  std::vector<SelectionOverlayLine> lines;
  std::string message;
};

/**
 * Stores the Poly Tool State data contract used by the modeling runtime domain.
 */
struct PolyToolState {
  bool active = false;
  bool closed = false;
  bool has_current = false;
  quader::QVec3 current_point;
  quader::QVec3 face_normal{0.0F, 1.0F, 0.0F};
  std::vector<quader::QVec3> points;
  std::vector<SelectionOverlayLine> preview_lines;
  std::vector<SelectionOverlayPoint> preview_points;
};

/**
 * Represents a Poly Tool Preview value used by the modeling runtime domain.
 */
struct PolyToolPreview {
  bool ok = true;
  bool active = false;
  bool closed = false;
  bool has_current = false;
  bool finalizable = false;
  std::uint32_t point_count = 0;
  std::uint32_t line_count = 0;
  quader::QVec3 first_point;
  quader::QVec3 last_point;
  quader::QVec3 current_point;
};

/**
 * Stores the Poly Tool Result data contract used by the modeling runtime
 * domain.
 */
struct PolyToolResult {
  bool ok = false;
  bool changed = false;
  std::string message;
};

/**
 * Stores the Pick Request data contract used by the modeling runtime domain.
 */
struct PickRequest {
  quader_poly::Ray ray;
  quader_poly::PickOptions options;
};

/**
 * Stores the Apply Pick Selection Request data contract used by the modeling
 * runtime domain.
 */
struct ApplyPickSelectionRequest {
  SelectionEditOperation operation = SelectionEditOperation::Replace;
  quader_poly::Ray ray;
  quader_poly::PickOptions pick_options;
  int click_count = 1;
  bool preserve_existing_component_selection_on_hit = false;
  bool preserve_selection_on_selected_hit = false;
  bool preserve_selection_on_miss = false;
  std::span<const PickObjectBounds> object_bounds;
  std::span<const PickObjectFaceGeometry> face_geometry;
};

/**
 * Represents an Apply Pick Selection Result value used by the modeling runtime
 * domain.
 */
struct ApplyPickSelectionResult {
  bool applied = true;
  bool changed = false;
  bool hit = false;
};

/**
 * Stores the Pick Hit Record data contract used by the modeling runtime domain.
 */
struct PickHitRecord {
  bool hit = false;
  int object_id = -1;
  int object_index = -1;
  int material_id = 0;
  quader_poly::ElementKind kind = quader_poly::ElementKind::Face;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  quader::QVec3 local_position;
  quader::QVec3 local_normal;
  float ray_distance = 0.0F;
  float pick_distance = 0.0F;
  std::string signature;
};

/**
 * Represents a Pick Selection Group value used by the modeling runtime domain.
 */
struct PickSelectionGroup {
  int object_id = -1;
  quader_poly::ElementKind kind = quader_poly::ElementKind::Face;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
  quader_poly::Edge edge;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
};

/**
 * Stores the Pick Selection State data contract used by the modeling runtime
 * domain.
 */
struct PickSelectionState {
  SelectionMode mode = SelectionMode::Object;
  std::vector<int> selected_object_ids;
  std::vector<PickSelectionGroup> selected_faces;
  std::vector<PickSelectionGroup> selected_edges;
  std::vector<PickSelectionGroup> selected_vertices;
};

/**
 * Represents a Rectangle Selection Face value used by the modeling runtime
 * domain.
 */
struct RectangleSelectionFace {
  int object_id = -1;
  int face_index = -1;
  std::vector<int> vertex_indices;
};

/**
 * Represents a Rectangle Selection Edge value used by the modeling runtime
 * domain.
 */
struct RectangleSelectionEdge {
  int object_id = -1;
  int a_index = -1;
  int b_index = -1;
};

/**
 * Represents a Rectangle Selection Vertex value used by the modeling runtime
 * domain.
 */
struct RectangleSelectionVertex {
  int object_id = -1;
  int vertex_index = -1;
};

/**
 * Stores the Rectangle Selection State data contract used by the modeling
 * runtime domain.
 */
struct RectangleSelectionState {
  SelectionMode mode = SelectionMode::Object;
  int active_object_id = -1;
  int active_face_index = -1;
  std::vector<int> selected_object_ids;
  std::vector<RectangleSelectionFace> selected_faces;
  std::vector<RectangleSelectionEdge> selected_edges;
  std::vector<int> active_edge_vertex_indices;
  std::vector<int> selected_edge_full_object_ids;
  std::vector<RectangleSelectionVertex> selected_vertices;
  std::vector<int> active_vertex_indices;
  std::vector<int> selected_vertex_full_object_ids;
};

/**
 * Represents a Rectangle Selection Universe value used by the modeling runtime
 * domain.
 */
struct RectangleSelectionUniverse {
  std::vector<int> object_ids;
  std::vector<RectangleSelectionFace> faces;
  std::vector<RectangleSelectionEdge> edges;
  std::vector<RectangleSelectionVertex> vertices;
};

/**
 * Represents a Rectangle Selection Hits value used by the modeling runtime
 * domain.
 */
struct RectangleSelectionHits {
  std::vector<int> object_ids;
  std::vector<RectangleSelectionFace> faces;
  std::vector<RectangleSelectionEdge> edges;
  std::vector<RectangleSelectionVertex> vertices;
};

/**
 * Stores the Rectangle Selection Request data contract used by the modeling
 * runtime domain.
 */
struct RectangleSelectionRequest {
  RectangleSelectionState current_selection;
  SelectionEditOperation operation = SelectionEditOperation::Replace;
  RectangleSelectionUniverse universe;
  RectangleSelectionHits hits;
};

/**
 * Stores the Rectangle Selection Result data contract used by the modeling
 * runtime domain.
 */
struct RectangleSelectionResult {
  RectangleSelectionState selection;
};

/**
 * Enumerates SelectionSetCommand values used by the modeling layer.
 */
enum class SelectionSetCommand {
  SelectAll,
  Invert,
};

/**
 * Stores the Selection Set Command Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionSetCommandRequest {
  RectangleSelectionState current_selection;
  RectangleSelectionUniverse universe;
  SelectionSetCommand command = SelectionSetCommand::SelectAll;
};

/**
 * Stores the Face Mode Conversion Request data contract used by the modeling
 * runtime domain.
 */
struct FaceModeConversionRequest {
  RectangleSelectionState current_selection;
  RectangleSelectionUniverse universe;
};

/**
 * Stores the Full Element Promotion Request data contract used by the modeling
 * runtime domain.
 */
struct FullElementPromotionRequest {
  RectangleSelectionState current_selection;
  RectangleSelectionUniverse universe;
  int preferred_active_object_id = -1;
};

/**
 * Stores the Full Element Promotion Result data contract used by the modeling
 * runtime domain.
 */
struct FullElementPromotionResult {
  bool promoted = false;
  RectangleSelectionState selection;
};

/**
 * Stores the Selection Object Ids Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionObjectIdsRequest {
  RectangleSelectionState current_selection;
  std::vector<int> object_ids;
  bool force_object_mode = false;
};

/**
 * Stores the Selection Object Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionObjectRequest {
  RectangleSelectionState current_selection;
  int object_id = -1;
  bool force_object_mode = false;
};

/**
 * Stores the Selection Face Request data contract used by the modeling runtime
 * domain.
 */
struct SelectionFaceRequest {
  RectangleSelectionState current_selection;
  RectangleSelectionFace face;
  bool force_face_mode = false;
};

/**
 * Stores the Selection Object Wide Elements Request data contract used by the
 * modeling runtime domain.
 */
struct SelectionObjectWideElementsRequest {
  RectangleSelectionState current_selection;
  int object_id = -1;
  SelectionMode mode = SelectionMode::Face;
  std::vector<RectangleSelectionFace> object_faces;
  bool additive = false;
};

/**
 * Represents a Selection Element Group value used by the modeling runtime
 * domain.
 */
struct SelectionElementGroup {
  int object_id = -1;
  std::vector<int> vertex_indices;
};

/**
 * Represents a Selection Active Target value used by the modeling runtime
 * domain.
 */
struct SelectionActiveTarget {
  bool has_active = false;
  ActiveSelectionKind kind = ActiveSelectionKind::None;
  SelectionMode selection_mode = SelectionMode::Face;
  int object_id = -1;
  int face_index = -1;
  std::vector<int> vertex_indices;
};

/**
 * Represents a Selection Handle Target value used by the modeling runtime
 * domain.
 */
struct SelectionHandleTarget {
  ActiveSelectionKind kind = ActiveSelectionKind::None;
  int object_id = -1;
  int face_index = -1;
  std::vector<int> vertex_indices;
  std::vector<SelectionElementGroup> element_groups;
  quader::QVec3 position;
};

/**
 * Represents a Selection Hit Target value used by the modeling runtime domain.
 */
struct SelectionHitTarget {
  int object_id = -1;
  int face_index = -1;
};

/**
 * Stores the Selection Handle Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionHandleRequest {
  RectangleSelectionState current_selection;
  SelectionHandleTarget handle;
  bool additive = false;
};

/**
 * Stores the Selection Hit Request data contract used by the modeling runtime
 * domain.
 */
struct SelectionHitRequest {
  RectangleSelectionState current_selection;
  SelectionHitTarget hit;
  bool additive = false;
};

/**
 * Stores the Selection Mode Switch Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionModeSwitchRequest {
  RectangleSelectionState current_selection;
  SelectionMode mode = SelectionMode::Face;
};

/**
 * Stores the Selection Feedback Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionFeedbackRequest {
  RectangleSelectionState current_selection;
  std::vector<int> compact_edge_object_ids;
  std::vector<int> preview_changed_object_ids;
};

/**
 * Represents a Selection Object Face Count value used by the modeling runtime
 * domain.
 */
struct SelectionObjectFaceCount {
  int object_id = -1;
  int face_count = 0;
};

/**
 * Stores the Selection Face Xray Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionFaceXrayRequest {
  RectangleSelectionState selection;
  std::vector<SelectionObjectFaceCount> object_face_counts;
};

/**
 * Represents a Selection Overlay Edge value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayEdge {
  int object_id = -1;
  int a_index = -1;
  int b_index = -1;
};

/**
 * Stores the Selection Overlay Lookup Request data contract used by the
 * modeling runtime domain.
 */
struct SelectionOverlayLookupRequest {
  RectangleSelectionState current_selection;
  std::vector<int> extra_target_object_ids;
  int hovered_object_id = -1;
};

/**
 * Represents a Selection Overlay Lookup value used by the modeling runtime
 * domain.
 */
struct SelectionOverlayLookup {
  std::vector<int> selected_object_ids;
  std::vector<int> target_object_ids;
  std::vector<RectangleSelectionFace> selected_faces;
  std::vector<SelectionOverlayEdge> selected_edges;
  std::vector<RectangleSelectionVertex> selected_vertices;
};

/**
 * Stores the Selection Overlay Policy Request data contract used by the
 * modeling runtime domain.
 */
struct SelectionOverlayPolicyRequest {
  RectangleSelectionState current_selection;
  int selected_vertex_limit = 2048;
  int selected_edge_limit = 4096;
  int vertex_references_per_full_object = 8;
  int edge_references_per_full_object = 12;
  bool suppress_selected_object_overlay = false;
  bool suppress_selected_object_detail = false;
};

/**
 * Stores the Selection Overlay Policy data contract used by the modeling
 * runtime domain.
 */
struct SelectionOverlayPolicy {
  bool element_mode = false;
  bool dense_selected_vertex_overlay = false;
  bool dense_selected_edge_overlay = false;
  bool dense_element_minimal_overlay = false;
  bool suppress_selected_object_feedback = false;
  bool should_build_lookup = true;
  int selected_vertex_reference_count = 0;
  int selected_edge_reference_count = 0;
  int element_target_reference_count = 0;
};

/**
 * Represents a Selection Source Wire Suppression value used by the modeling
 * runtime domain.
 */
struct SelectionSourceWireSuppression {
  std::vector<int> hidden_object_ids;
  std::vector<RectangleSelectionFace> hidden_faces;
  std::vector<SelectionOverlayEdge> hidden_edges;
};

/**
 * Enumerates SelectionCenterStrategy values used by the modeling layer.
 */
enum class SelectionCenterStrategy {
  None,
  SelectedObjects,
  DenseFullObjects,
  SparseElements,
  FallbackPoints,
};

/**
 * Stores the Selection Center Plan Request data contract used by the modeling
 * runtime domain.
 */
struct SelectionCenterPlanRequest {
  RectangleSelectionState current_selection;
  int dense_center_reference_limit = 2048;
  int vertex_references_per_full_object = 8;
  int edge_references_per_full_object = 12;
};

/**
 * Represents a Selection Center Plan value used by the modeling runtime domain.
 */
struct SelectionCenterPlan {
  RectangleSelectionState normalized_selection;
  SelectionCenterStrategy strategy = SelectionCenterStrategy::None;
  std::vector<int> object_ids;
  bool ignored_selected_objects_for_element_entries = false;
  bool has_element_entries = false;
  int dense_reference_count = 0;
};

/**
 * Stores the Selection Handle Target Object Request data contract used by the
 * modeling runtime domain.
 */
struct SelectionHandleTargetObjectRequest {
  RectangleSelectionState current_selection;
  std::vector<int> extra_target_object_ids;
  int hovered_object_id = -1;
};

/**
 * Enumerates ElementHandlePickKind values used by the modeling layer.
 */
enum class ElementHandlePickKind {
  Vertex = 0,
  Edge = 1,
  Face = 2,
};

/**
 * Enumerates ViewportToolIntent values used by the modeling layer.
 */
enum class ViewportToolIntent {
  Move = 0,
  Extend = 1,
};

/**
 * Represents an Element Handle Pick Candidate value used by the modeling
 * runtime domain.
 */
struct ElementHandlePickCandidate {
  int candidate_index = -1;
  ElementHandlePickKind kind = ElementHandlePickKind::Vertex;
  int object_id = -1;
  int a_index = -1;
  int b_index = -1;
  quader::QVec2 screen_position;
  double camera_distance = 0.0;
};

/**
 * Represents an Element Handle Pick Hit value used by the modeling runtime
 * domain.
 */
struct ElementHandlePickHit {
  int candidate_index = -1;
  double screen_distance = 0.0;
  double screen_distance_squared = 0.0;
};

/**
 * Stores the Selection Hover Feedback Request data contract used by the
 * modeling runtime domain.
 */
struct SelectionHoverFeedbackRequest {
  RectangleSelectionState current_selection;
  SelectionHandleTarget hovered_handle;
  SelectionHitTarget hovered_hit;
  bool selected_modifier_pressed = false;
};

/**
 * Represents a Selection Hover Feedback value used by the modeling runtime
 * domain.
 */
struct SelectionHoverFeedback {
  RectangleSelectionState highlight_selection;
  bool hover_target_selected = false;
};

/**
 * Stores the Pick Cycle State data contract used by the modeling runtime
 * domain.
 */
struct PickCycleState {
  bool active = false;
  quader::QVec2 screen_position;
  std::string signature;
  int index = 0;
};

/**
 * Represents a Pick Cycle Step value used by the modeling runtime domain.
 */
struct PickCycleStep {
  bool active = false;
  bool cycled = false;
  int index = 0;
};

/**
 * Enumerates PickHitSet values used by the modeling layer.
 */
enum class PickHitSet {
  None,
  Handle,
  Surface,
};

/**
 * Stores the Click Pick Request data contract used by the modeling runtime
 * domain.
 */
struct ClickPickRequest {
  SelectionMode mode = SelectionMode::Object;
  SelectionEditOperation operation = SelectionEditOperation::Replace;
  bool handles_enabled = false;
  bool can_pick_all = true;
  quader::QVec2 screen_position;
  float max_screen_distance = kPickCycleDefaultMaxScreenDistance;
};

/**
 * Represents a Click Pick Plan value used by the modeling runtime domain.
 */
struct ClickPickPlan {
  bool has_target = false;
  PickHitSet hit_set = PickHitSet::None;
  int hit_index = -1;
  bool preserve_selection = false;
  bool clear_with_handle_state_on_miss = false;
  bool used_pick_cycle = false;
  PickCycleStep cycle;
};

/**
 * Stores the Drag Seed Pick Request data contract used by the modeling runtime
 * domain.
 */
struct DragSeedPickRequest {
  SelectionMode mode = SelectionMode::Object;
  bool additive = false;
  bool handles_enabled = false;
  bool allow_selected_target = false;
  bool preserve_selected_additive_target = false;
};

/**
 * Represents a Drag Seed Pick Plan value used by the modeling runtime domain.
 */
struct DragSeedPickPlan {
  bool has_target = false;
  bool keep_existing_selection = false;
  PickHitSet hit_set = PickHitSet::None;
  int hit_index = -1;
  int preferred_object_id = -1;
};

/**
 * Stores the Command Result data contract used by the modeling runtime domain.
 */
struct CommandResult {
  bool ok = true;
  bool changed = false;
  std::string message;
};

/**
 * Represents a History Status value used by the modeling runtime domain.
 */
struct HistoryStatus {
  bool can_undo = false;
  bool can_redo = false;
  std::uint32_t undo_count = 0;
  std::uint32_t redo_count = 0;
};

/**
 * Stores the History Restore Result data contract used by the modeling runtime
 * domain.
 */
struct HistoryRestoreResult {
  bool applied = false;
  HistoryStatus status;
};

/**
 * Enumerates PreviewTransactionKind values used by the modeling layer.
 */
enum class PreviewTransactionKind {
  None = 0,
  Bridge = 1,
  EdgeBevel = 2,
  Thicken = 3,
  Transform = 4,
  SliceQuad = 5,
  MergeByDistance = 6,
};

/**
 * Represents a Preview Transaction Status value used by the modeling runtime
 * domain.
 */
struct PreviewTransactionStatus {
  PreviewTransactionKind kind = PreviewTransactionKind::None;
  bool active = false;
  bool changed = false;
  std::uint32_t merge_by_distance_removed_vertices = 0;
};

/**
 * Stores the Preview Transaction Finish Result data contract used by the
 * modeling runtime domain.
 */
struct PreviewTransactionFinishResult {
  PreviewTransactionStatus before;
  bool restored = false;
};

/**
 * Stores the Session State data contract used by the modeling runtime domain.
 */
struct SessionState {
  std::vector<PolygonObject> objects;
  std::vector<LightEntity> lights;
  int next_object_id = 1;
  int next_light_id = 1;
  int active_object_id = -1;
  int active_light_id = -1;
  SelectionMode selection_mode = SelectionMode::Object;
  bool dirty = false;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Represents a History Entry value used by the modeling runtime domain.
 */
struct HistoryEntry {
  SessionState before;
  SessionState after;
};

/**
 * Represents a Preview Transaction value used by the modeling runtime domain.
 */
struct PreviewTransaction {
  PreviewTransactionKind kind = PreviewTransactionKind::None;
  bool active = false;
  bool changed = false;
  std::uint32_t merge_by_distance_removed_vertices = 0;
  SessionState base_state;
  SessionState history_state;
};

SessionState default_scene_state();

SelectionMode sanitize_selection_mode(int mode);
SelectionEditOperation sanitize_selection_operation(int operation);
quader_poly::SelectionMode component_selection_mode(SelectionMode mode);
bool component_selection_has_mode_components(
    const quader_poly::Selection &selection, SelectionMode mode);
bool polygon_selection_matches(const quader_poly::Selection &left,
                               const quader_poly::Selection &right);

int selected_object_index(const std::vector<PolygonObject> &objects,
                          int active_object_id);
int selected_object_id(const std::vector<PolygonObject> &objects,
                       int active_object_id);
std::vector<int> selected_object_ids(const std::vector<PolygonObject> &objects);
void normalize_active_object_id(const std::vector<PolygonObject> &objects,
                                int &active_object_id);
int next_light_entity_id(const std::vector<LightEntity> &lights,
                         int next_light_id = 1);
void normalize_active_light_id(const std::vector<LightEntity> &lights,
                               int &active_light_id);
std::vector<int> selected_light_ids(const std::vector<LightEntity> &lights);

void set_selection_mode(std::vector<PolygonObject> &objects, SelectionMode mode,
                        int &active_object_id);
void clear_selection(std::vector<PolygonObject> &objects,
                     int &active_object_id);
bool has_selection(const std::vector<PolygonObject> &objects,
                   SelectionMode mode, int active_object_id);
std::optional<quader::QVec3>
selection_center(const std::vector<PolygonObject> &objects, SelectionMode mode,
                 int active_object_id);
ActiveSelection active_selection(const std::vector<PolygonObject> &objects,
                                 SelectionMode mode, int active_object_id);
int next_polygon_object_id(const std::vector<PolygonObject> &objects,
                           int next_object_id = 1);

std::optional<PickHitRecord>
pick_nearest(const std::vector<PolygonObject> &objects,
             const PickRequest &request);
std::optional<PickHitRecord>
pick_nearest(const std::vector<PolygonObject> &objects,
             const PickRequest &request,
             std::span<const PickObjectBounds> object_bounds,
             std::span<const PickObjectFaceGeometry> face_geometry);
std::vector<PickHitRecord> pick_all(const std::vector<PolygonObject> &objects,
                                    const PickRequest &request);
std::vector<PickHitRecord>
pick_all(const std::vector<PolygonObject> &objects, const PickRequest &request,
         std::span<const PickObjectBounds> object_bounds,
         std::span<const PickObjectFaceGeometry> face_geometry);
quader_poly::PickOptions selection_pick_options_from_radii(double vertex_radius,
                                                           double edge_radius);
std::vector<PickObjectFaceGeometry>
build_pick_face_geometry(const std::vector<PolygonObject> &objects);
std::vector<PickObjectFaceGeometry>
build_pick_face_geometry(const std::vector<PolygonObject> &objects,
                         quader_poly::PickOptions pick_options);
std::string pick_hit_signature(const PickHitRecord &hit);
std::string pick_hits_signature(const std::vector<PickHitRecord> &hits);
bool pick_hit_target_is_selected(const PickSelectionState &selection,
                                 const PickHitRecord &hit);
bool pick_surface_hit_target_is_selected(const PickSelectionState &selection,
                                         const PickHitRecord &hit);
bool pick_handle_target_is_selected(const PickSelectionState &selection,
                                    const PickHitRecord &hit);
std::optional<int>
selected_pick_hit_index(const PickSelectionState &selection,
                        const std::vector<PickHitRecord> &hits);
std::optional<int>
first_unselected_pick_hit_index(const PickSelectionState &selection,
                                const std::vector<PickHitRecord> &hits);
std::optional<int>
selected_surface_pick_hit_index(const PickSelectionState &selection,
                                const std::vector<PickHitRecord> &hits);
std::optional<int>
front_unselected_surface_pick_hit_index(const PickSelectionState &selection,
                                        const std::vector<PickHitRecord> &hits);
std::optional<int>
selected_handle_pick_hit_index(const PickSelectionState &selection,
                               const std::vector<PickHitRecord> &hits);
std::optional<int>
front_unselected_handle_pick_hit_index(const PickSelectionState &selection,
                                       const std::vector<PickHitRecord> &hits);
std::vector<int>
pick_cycle_candidate_indices(const std::vector<PickHitRecord> &hits,
                             bool keep_all_component_hits);
void reset_pick_cycle(PickCycleState &state);
PickCycleStep advance_pick_cycle(PickCycleState &state, std::string signature,
                                 int candidate_count,
                                 quader::QVec2 screen_position,
                                 float max_screen_distance);
ClickPickPlan plan_click_pick(PickCycleState &state,
                              const PickSelectionState &selection,
                              const std::vector<PickHitRecord> &handle_hits,
                              const std::vector<PickHitRecord> &surface_hits,
                              const ClickPickRequest &request);
DragSeedPickPlan
plan_drag_seed_pick(const PickSelectionState &selection,
                    const std::vector<PickHitRecord> &handle_hits,
                    const std::vector<PickHitRecord> &surface_hits,
                    const DragSeedPickRequest &request);
RectangleSelectionResult
apply_rectangle_selection(const RectangleSelectionRequest &request);
RectangleSelectionResult
apply_selection_set_command(const SelectionSetCommandRequest &request);
RectangleSelectionResult
convert_selection_to_face_mode(const FaceModeConversionRequest &request);
FullElementPromotionResult promote_full_element_selection_to_object(
    const FullElementPromotionRequest &request);
RectangleSelectionResult
select_object_ids(const SelectionObjectIdsRequest &request);
RectangleSelectionResult select_object(const SelectionObjectRequest &request);
RectangleSelectionResult select_face(const SelectionFaceRequest &request);
RectangleSelectionResult
select_object_wide_elements(const SelectionObjectWideElementsRequest &request);
SelectionActiveTarget
active_target_from_selection(const RectangleSelectionState &selection);
RectangleSelectionResult
clear_empty_element_click_selection(const RectangleSelectionState &selection);
RectangleSelectionResult select_handle(const SelectionHandleRequest &request);
RectangleSelectionResult select_hit(const SelectionHitRequest &request);
RectangleSelectionResult
switch_selection_mode(const SelectionModeSwitchRequest &request);
std::vector<int>
selected_or_active_object_ids(const RectangleSelectionState &selection);
int selection_count(const RectangleSelectionState &selection);
bool selection_has_any(const RectangleSelectionState &selection);
bool selection_has_object_selection(const RectangleSelectionState &selection);
bool selection_has_face_selection(const RectangleSelectionState &selection);
bool selection_has_edge_selection(const RectangleSelectionState &selection);
bool selection_has_vertex_selection(const RectangleSelectionState &selection);
bool selection_has_gpu_feedback(const RectangleSelectionState &selection);
std::vector<int>
dirty_object_ids_for_selection(const RectangleSelectionState &selection);
RectangleSelectionState
gpu_feedback_selection(const RectangleSelectionState &selection);
RectangleSelectionState
gpu_feedback_selection(const SelectionFeedbackRequest &request);
RectangleSelectionState
gpu_wire_feedback_selection(const RectangleSelectionState &selection);
RectangleSelectionState
gpu_wire_feedback_selection(const SelectionFeedbackRequest &request);
RectangleSelectionState
preview_element_handle_mesh_selection(const RectangleSelectionState &selection);
RectangleSelectionState
preview_element_handle_mesh_selection(const SelectionFeedbackRequest &request);
RectangleSelectionState without_cpu_element_handle_overlay_selection(
    const RectangleSelectionState &selection);
RectangleSelectionState
empty_feedback_selection(const RectangleSelectionState &selection);
RectangleSelectionState
face_feedback_selection(const RectangleSelectionState &selection);
RectangleSelectionState
visual_face_feedback_selection(const SelectionFeedbackRequest &request);
bool selection_face_feedback_should_xray(
    const RectangleSelectionState &selection,
    const std::vector<SelectionObjectFaceCount> &object_face_counts);
bool selection_face_feedback_should_xray(
    const SelectionFaceXrayRequest &request);
SelectionOverlayLookup
build_selection_overlay_lookup(const RectangleSelectionState &selection);
SelectionOverlayLookup
build_selection_overlay_lookup(const SelectionOverlayLookupRequest &request);
bool selection_overlay_lookup_has_selected_object(
    const SelectionOverlayLookup &lookup, int object_id);
bool selection_overlay_lookup_targets_object(
    const SelectionOverlayLookup &lookup, int object_id);
bool selection_overlay_lookup_has_selected_face(
    const SelectionOverlayLookup &lookup, RectangleSelectionFace face);
bool selection_overlay_lookup_has_selected_edge(
    const SelectionOverlayLookup &lookup, SelectionOverlayEdge edge);
bool selection_overlay_lookup_has_selected_vertex(
    const SelectionOverlayLookup &lookup, RectangleSelectionVertex vertex);
SelectionOverlayPolicy
selection_overlay_policy(const SelectionOverlayPolicyRequest &request);
SelectionSourceWireSuppression
source_wire_suppression_for_selection(const RectangleSelectionState &selection);
std::vector<int> object_operation_object_ids_for_selection(
    const RectangleSelectionState &selection);
SelectionCenterPlan
plan_selection_center(const SelectionCenterPlanRequest &request);
std::vector<int> handle_target_object_ids_for_selection(
    const SelectionHandleTargetObjectRequest &request);
std::vector<ElementHandlePickHit> pick_element_handle_candidates(
    const std::vector<ElementHandlePickCandidate> &candidates,
    quader::QVec2 screen_position, double pick_radius);
bool selection_mode_uses_element_handles(SelectionMode mode);
ViewportToolIntent default_viewport_tool_for_selection_mode(SelectionMode mode);
bool selection_has_move_tool_target(const RectangleSelectionState &selection);
SelectionHoverFeedback
hover_feedback_for_selection(const SelectionHoverFeedbackRequest &request);
bool selection_has_object(const RectangleSelectionState &selection,
                          int object_id);
bool selection_has_face(const RectangleSelectionState &selection,
                        RectangleSelectionFace face);
bool selection_has_edge(const RectangleSelectionState &selection,
                        RectangleSelectionEdge edge);
bool selection_has_vertex(const RectangleSelectionState &selection,
                          RectangleSelectionVertex vertex);
std::vector<int>
source_object_ids_for_hit(const RectangleSelectionState &selection,
                          int clicked_object_id);
std::vector<int>
target_object_ids_for_hit(const RectangleSelectionState &selection,
                          int object_id);
std::vector<int>
whole_object_transform_ids(const RectangleSelectionState &selection);
std::optional<RectangleSelectionState>
whole_object_transform_selection(const RectangleSelectionState &selection);
bool selection_hover_handle_changed(const SelectionHandleTarget &current_handle,
                                    const SelectionHandleTarget &next_handle);
bool selection_hover_hit_changed(const SelectionHitTarget &current_hit,
                                 const SelectionHitTarget &next_hit);
SelectionHoverUpdateResult update_selection_hover(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    const SelectionHoverState &current_hover, const quader_poly::Ray &ray,
    bool selected_modifier_pressed,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {});
bool selection_hover_target_matches_selection(
    std::span<const PolygonObject> objects, SelectionMode mode,
    const SelectionHoverState &hover);
bool selection_hover_target_matches_selection(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    const SelectionHoverState &hover);
bool selection_hover_target_is_selected(std::span<const PolygonObject> objects,
                                        SelectionMode mode,
                                        const SelectionHoverState &hover);
bool selection_hover_target_is_selected(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    const SelectionHoverState &hover);
SelectionOverlayFrame build_selection_overlay_frame(
    std::span<const PolygonObject> objects, SelectionMode mode,
    const SelectionHoverState &hover, const SelectionOverlayStyle &style = {});
SelectionOverlayFrame build_selection_overlay_frame(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    const SelectionHoverState &hover, const SelectionOverlayStyle &style = {});
SelectionOverlayFrame build_selection_overlay_frame(
    std::span<const PolygonObject> objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover,
    const SelectionOverlayStyle &style = {});
SelectionOverlayFrame build_selection_overlay_frame(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    int active_object_id, const SelectionHoverState &hover,
    const SelectionOverlayStyle &style = {});
SelectionOverlayFrame build_selection_hover_overlay_frame(
    std::span<const PolygonObject> objects, SelectionMode mode,
    const SelectionHoverState &hover, const SelectionOverlayStyle &style = {});
SelectionOverlayFrame build_selection_hover_overlay_frame(
    const std::vector<PolygonObject> &objects, SelectionMode mode,
    const SelectionHoverState &hover, const SelectionOverlayStyle &style = {});
SelectionOverlayFrame
build_diagnostic_edge_overlay_frame(std::span<const PolygonObject> objects,
                                    const SelectionOverlayStyle &style = {});
SelectionOverlayFrame
build_diagnostic_edge_overlay_frame(const std::vector<PolygonObject> &objects,
                                    const SelectionOverlayStyle &style = {});

bool pick_select(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    bool allow_component_fallback = true,
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {});
ApplyPickSelectionResult
apply_pick_selection(EditorSession &session,
                     std::vector<PolygonObject> picking_objects,
                     const ApplyPickSelectionRequest &request);
bool pick_select_face(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions());
bool pick_select_all_faces(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {});
bool pick_select_edge_loop(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {});
bool pick_select_all_vertices(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {});

Bounds3 document_bounds(const std::vector<PolygonObject> &objects);
Bounds3 selection_bounds(const std::vector<PolygonObject> &objects,
                         SelectionMode mode);

CommandResult select_all(std::vector<PolygonObject> &objects,
                         SelectionMode mode, int &active_object_id);
CommandResult invert_selection(std::vector<PolygonObject> &objects,
                               SelectionMode mode, int &active_object_id);
CommandResult translate_selected(std::vector<PolygonObject> &objects,
                                 SelectionMode mode, quader::QVec3 delta,
                                 bool texture_lock_enabled);
CommandResult transform_selected(std::vector<PolygonObject> &objects,
                                 SelectionMode mode,
                                 const quader_poly::Transform3 &transform,
                                 quader::QVec3 pivot, bool texture_lock_enabled,
                                 bool texture_lock_scale_enabled);
CommandResult transform_selected(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    std::span<const ObjectLocalTransform> transforms,
    bool texture_lock_enabled, bool texture_lock_scale_enabled);
CommandResult
snap_selected_vertices_to_active(std::vector<PolygonObject> &objects,
                                 SelectionMode mode, int &active_object_id);
CommandResult snap_selected_edges_to_active(std::vector<PolygonObject> &objects,
                                            SelectionMode mode,
                                            int &active_object_id);
CommandResult
merge_selected_vertices_to_active(std::vector<PolygonObject> &objects,
                                  SelectionMode mode, int &active_object_id);
CommandResult
merge_selected_vertices_to_center(std::vector<PolygonObject> &objects,
                                  SelectionMode mode, int &active_object_id);
CommandResult
merge_selected_vertices_by_distance(std::vector<PolygonObject> &objects,
                                    SelectionMode mode, int &active_object_id,
                                    double tolerance);
CommandResult remove_doubles(std::vector<PolygonObject> &objects,
                             SelectionMode mode, int &active_object_id);
CommandResult bevel_selected_vertices(std::vector<PolygonObject> &objects,
                                      SelectionMode mode, int &active_object_id,
                                      float distance);
CommandResult connect_selected_vertices(std::vector<PolygonObject> &objects,
                                        SelectionMode mode,
                                        int &active_object_id);
CommandResult dissolve_selected_vertices(std::vector<PolygonObject> &objects,
                                         SelectionMode mode,
                                         int &active_object_id);
CommandResult connect_selected_edges(std::vector<PolygonObject> &objects,
                                     SelectionMode mode, int &active_object_id);
CommandResult split_selected_edges(std::vector<PolygonObject> &objects,
                                   SelectionMode mode, int &active_object_id);
CommandResult dissolve_selected_edges(std::vector<PolygonObject> &objects,
                                      SelectionMode mode,
                                      int &active_object_id);
CommandResult merge_selected_edges(std::vector<PolygonObject> &objects,
                                   SelectionMode mode, int &active_object_id);
CommandResult collapse_selected_edges(std::vector<PolygonObject> &objects,
                                      SelectionMode mode,
                                      int &active_object_id);
CommandResult fill_hole_from_selected_edges(std::vector<PolygonObject> &objects,
                                            SelectionMode mode,
                                            int &active_object_id);
CommandResult
bevel_selected_edges(std::vector<PolygonObject> &objects, SelectionMode mode,
                     int &active_object_id,
                     const quader_poly::EdgeBevelSettings &settings);
CommandResult bridge_selected_edges(std::vector<PolygonObject> &objects,
                                    SelectionMode mode, int &active_object_id,
                                    int steps = 1);
CommandResult delete_selected(std::vector<PolygonObject> &objects,
                              SelectionMode mode, int &active_object_id);
CommandResult create_box_from_bounds(std::vector<PolygonObject> &objects,
                                     int object_id, quader::QVec3 min_bounds,
                                     quader::QVec3 max_bounds,
                                     int &active_object_id,
                                     SelectionMode &selection_mode);
CommandResult create_box_from_corners(std::vector<PolygonObject> &objects,
                                      int object_id,
                                      std::span<const quader::QVec3> corners,
                                      int &active_object_id,
                                      SelectionMode &selection_mode);
CommandResult create_polygon_from_points(std::vector<PolygonObject> &objects,
                                         int object_id,
                                         std::span<const quader::QVec3> points,
                                         quader::QVec3 face_normal,
                                         int &active_object_id,
                                         SelectionMode &selection_mode);
CommandResult extract_selected_faces(std::vector<PolygonObject> &objects,
                                     int object_id, SelectionMode mode,
                                     int &active_object_id);
CommandResult detach_selected_faces(std::vector<PolygonObject> &objects,
                                    SelectionMode mode, int &active_object_id);
CommandResult bridge_selected_faces(std::vector<PolygonObject> &objects,
                                    SelectionMode mode, int &active_object_id,
                                    int steps = 1);
CommandResult thicken_selected_faces(std::vector<PolygonObject> &objects,
                                     SelectionMode mode, int &next_object_id,
                                     int &active_object_id, float thickness,
                                     bool from_center);
CommandResult slice_selected_quads(std::vector<PolygonObject> &objects,
                                   SelectionMode mode, int &active_object_id,
                                   int x_slices, int y_slices);
CommandResult combine_selected_faces(std::vector<PolygonObject> &objects,
                                     SelectionMode mode, int &active_object_id);
CommandResult combine_selected_meshes(std::vector<PolygonObject> &objects,
                                      SelectionMode mode,
                                      int &active_object_id);
CommandResult collapse_selected_faces(std::vector<PolygonObject> &objects,
                                      SelectionMode mode,
                                      int &active_object_id);
CommandResult radial_align_selected(std::vector<PolygonObject> &objects,
                                    SelectionMode mode, int &active_object_id);
CommandResult extrude_selected_elements(std::vector<PolygonObject> &objects,
                                        SelectionMode mode,
                                        quader::QVec3 offset,
                                        float closed_edge_ledge_size = 0.0F);
CommandResult extrude_selected_elements(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    std::span<const ObjectLocalOffset> offsets,
    float closed_edge_ledge_size = 0.0F);
CommandResult
transform_extrude_selected_elements(std::vector<PolygonObject> &objects,
                                    SelectionMode mode,
                                    const quader_poly::Transform3 &transform,
                                    quader::QVec3 pivot);
CommandResult transform_extrude_selected_elements(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    std::span<const ObjectLocalTransform> transforms);
CommandResult inset_selected_elements(std::vector<PolygonObject> &objects,
                                      SelectionMode mode,
                                      const quader_poly::Transform3 &transform,
                                      quader::QVec3 pivot);
CommandResult inset_selected_elements(
    std::vector<PolygonObject> &objects, SelectionMode mode,
    std::span<const ObjectLocalTransform> transforms);
CommandResult extrude_selected_faces(std::vector<PolygonObject> &objects,
                                     float distance);
CommandResult inset_selected_faces(std::vector<PolygonObject> &objects,
                                   float amount);
CommandResult insert_edge_loop(std::vector<PolygonObject> &objects,
                               float factor);
CommandResult insert_edge_loop(std::vector<PolygonObject> &objects,
                               int object_id, quader_poly::Edge edge,
                               float factor, int &active_object_id,
                               SelectionMode &mode);
CommandResult plane_cut(std::vector<PolygonObject> &objects,
                        std::span<const int> object_ids,
                        const quader_poly::PlaneCutRequest &request,
                        int &next_object_id, int &active_object_id,
                        SelectionMode &mode);
CommandResult mirror_objects_across_plane(
    std::vector<PolygonObject> &objects, std::span<const int> object_ids,
    std::span<const quader::QVec3> plane_points, int &next_object_id,
    int &active_object_id, SelectionMode &mode);
CommandResult
mirror_objects_across_world_axis(std::vector<PolygonObject> &objects,
                                 std::span<const int> object_ids,
                                 TransformAxis axis, int &next_object_id,
                                 int &active_object_id, SelectionMode &mode);
CommandResult flip_objects_by_orientation(std::vector<PolygonObject> &objects,
                                          std::span<const int> object_ids,
                                          SelectionMode &mode,
                                          ViewportFlipOrientation orientation,
                                          int &active_object_id);
InsertEdgeLoopPreview
build_insert_edge_loop_preview(const std::vector<PolygonObject> &objects,
                               int object_id, quader_poly::Edge edge,
                               float factor);
bool insert_edge_loop_tool_has_seed(const InsertEdgeLoopToolState &state);
void reset_insert_edge_loop_tool(InsertEdgeLoopToolState &state);
void clear_insert_edge_loop_preview(InsertEdgeLoopToolState &state,
                                    SelectionHoverState &hover, int object_id,
                                    quader_poly::Edge edge, float factor,
                                    bool dragging);
bool set_insert_edge_loop_preview(InsertEdgeLoopToolState &state,
                                  const std::vector<PolygonObject> &objects,
                                  SelectionHoverState &hover, int object_id,
                                  quader_poly::Edge edge, float factor,
                                  bool dragging);
bool update_insert_edge_loop_locked_seed(
    InsertEdgeLoopToolState &state, const std::vector<PolygonObject> &objects,
    SelectionHoverState &hover, const quader_poly::Ray &ray, double grid_size,
    bool snap_enabled, bool dragging);
KnifeToolPreview knife_tool_preview(const KnifeToolState &state);
void reset_knife_tool(KnifeToolState &state);
void rebuild_knife_overlay(KnifeToolState &state);
bool update_knife_preview_from_pick(
    EditorSession &session, KnifeToolState &state, SelectionHoverState &hover,
    const quader_poly::Ray &ray, const ViewportCameraState &camera,
    double grid_size, bool snap_enabled, bool constrain_axis);
void refresh_knife_intersection_preview(EditorSession &session,
                                        KnifeToolState &state,
                                        const ViewportCameraState &camera);
bool accept_knife_current_point(EditorSession &session, KnifeToolState &state,
                                const ViewportCameraState &camera,
                                double grid_size);
CommandResult knife_segment(std::vector<PolygonObject> &objects, int object_id,
                            const quader_poly::KnifePointTarget &previous,
                            const quader_poly::KnifePointTarget &current,
                            int &active_object_id, SelectionMode &mode);
CommandResult
knife_stroke(std::vector<PolygonObject> &objects, int object_id,
             std::span<const quader_poly::KnifePointTarget> points,
             std::span<const quader_poly::KnifeStrokeSegment> segments,
             int &active_object_id, SelectionMode &mode);
KnifeSegmentPreview
build_knife_segment_preview(const std::vector<PolygonObject> &objects,
                            int object_id,
                            const quader_poly::KnifePointTarget &previous,
                            const quader_poly::KnifePointTarget &current);
CommandResult flip_selected_face_normals(std::vector<PolygonObject> &objects,
                                         SelectionMode mode);
CommandResult
recalculate_selected_face_normals(std::vector<PolygonObject> &objects,
                                  SelectionMode mode, bool outside);
CommandResult shade_selected_faces_smooth(std::vector<PolygonObject> &objects,
                                          SelectionMode mode);
CommandResult shade_selected_faces_flat(std::vector<PolygonObject> &objects,
                                        SelectionMode mode);
CommandResult harden_selected_edge_normals(std::vector<PolygonObject> &objects,
                                           SelectionMode mode);
CommandResult soften_selected_edge_normals(std::vector<PolygonObject> &objects,
                                           SelectionMode mode);
CommandResult apply_uv_operation(std::vector<PolygonObject> &objects,
                                 SelectionMode mode, std::string_view operation,
                                 float shift_world_amount);
CommandResult assign_material_to_selection(std::vector<PolygonObject> &objects,
                                           SelectionMode mode,
                                           std::uint32_t material_slot);
CommandResult assign_material_to_face(std::vector<PolygonObject> &objects,
                                      int object_id,
                                      quader_poly::ElementId face_id,
                                      std::uint32_t material_slot);
std::vector<PolygonObject>
selected_object_copies(const std::vector<PolygonObject> &objects,
                       SelectionMode mode);
CommandResult duplicate_selected_objects(std::vector<PolygonObject> &objects,
                                         int &next_object_id,
                                         SelectionMode &mode,
                                         int &active_object_id,
                                         quader::QVec3 offset);
CommandResult paste_objects(std::vector<PolygonObject> &objects,
                            int &next_object_id, SelectionMode &mode,
                            int &active_object_id,
                            const std::vector<PolygonObject> &copied_objects,
                            std::optional<quader::QVec3> target_center);

/**
 * Owns Editor Session behavior within the modeling runtime domain.
 */
class EditorSession {
public:
  EditorSession() = default;
  explicit EditorSession(SessionState state);
  explicit EditorSession(std::vector<PolygonObject> objects);

  [[nodiscard]] const SessionState &state() const;
  [[nodiscard]] SessionState &state();
  void set_state(SessionState state);
  void reset_to_default_scene();

  [[nodiscard]] const std::vector<PolygonObject> &objects() const;
  [[nodiscard]] std::vector<PolygonObject> &objects();
  void set_objects(std::vector<PolygonObject> objects);
  [[nodiscard]] const std::vector<LightEntity> &lights() const;
  [[nodiscard]] std::vector<LightEntity> &lights();

  [[nodiscard]] int next_object_id() const;
  [[nodiscard]] int next_light_id() const;
  [[nodiscard]] int active_object_id() const;
  [[nodiscard]] int active_light_id() const;
  [[nodiscard]] SelectionMode selection_mode() const;
  [[nodiscard]] bool dirty() const;
  void mark_clean();

  [[nodiscard]] HistoryStatus history_status() const;
  void clear_history();
  void record_history(SessionState before);
  template <typename Command>
  CommandResult apply_history_command(Command &&command) {
    SessionState before = state_;
    CommandResult result = std::forward<Command>(command)(*this);
    if (!result.ok) {
      set_state(std::move(before));
      return result;
    }
    if (result.changed) {
      record_history(std::move(before));
    }
    return result;
  }
  template <typename Command>
  CommandResult apply_history_command_from(SessionState before,
                                           Command &&command) {
    CommandResult result = std::forward<Command>(command)(*this);
    if (!result.ok) {
      set_state(std::move(before));
      return result;
    }
    if (result.changed) {
      record_history(std::move(before));
    }
    return result;
  }
  HistoryRestoreResult undo();
  HistoryRestoreResult redo();
  [[nodiscard]] PreviewTransactionStatus preview_transaction_status() const;
  [[nodiscard]] std::optional<SessionState>
  preview_transaction_history_state(PreviewTransactionKind expected_kind =
                                        PreviewTransactionKind::None) const;
  void clear_preview_transaction();
  bool discard_preview_transactions(bool restore_transform_preview = false);
  bool restore_preview_transaction_base_for_edit(
      PreviewTransactionKind expected_kind = PreviewTransactionKind::None);
  bool capture_preview_transaction_base_after_edit(
      PreviewTransactionKind expected_kind = PreviewTransactionKind::None);
  CommandResult begin_bridge_preview(int steps = 1);
  CommandResult begin_bridge_preview(BridgeOperationSettings settings);
  CommandResult update_bridge_preview(int steps = 1);
  CommandResult update_bridge_preview(BridgeOperationSettings settings);
  CommandResult begin_bridge_preview_from_working_state(
      SessionState preview_base, SessionState working_base, int steps = 1);
  CommandResult update_bridge_preview_from_working_state(
      SessionState preview_base, SessionState working_base, int steps = 1);
  CommandResult
  begin_edge_bevel_preview(const quader_poly::EdgeBevelSettings &settings = {});
  CommandResult begin_edge_bevel_preview(EdgeBevelOperationSettings settings);
  CommandResult update_edge_bevel_preview(
      const quader_poly::EdgeBevelSettings &settings = {});
  CommandResult update_edge_bevel_preview(EdgeBevelOperationSettings settings);
  CommandResult begin_thicken_preview(float thickness, bool from_center);
  CommandResult begin_thicken_preview(ThickenOperationSettings settings);
  CommandResult update_thicken_preview(float thickness, bool from_center);
  CommandResult update_thicken_preview(ThickenOperationSettings settings);
  CommandResult begin_slice_quad_preview(int x_slices = 1, int y_slices = 1);
  CommandResult begin_slice_quad_preview(SliceQuadOperationSettings settings);
  CommandResult update_slice_quad_preview(int x_slices = 1, int y_slices = 1);
  CommandResult update_slice_quad_preview(SliceQuadOperationSettings settings);
  CommandResult begin_merge_by_distance_preview(double tolerance);
  CommandResult
  begin_merge_by_distance_preview(MergeByDistanceOperationSettings settings);
  CommandResult update_merge_by_distance_preview(double tolerance);
  CommandResult
  update_merge_by_distance_preview(MergeByDistanceOperationSettings settings);
  CommandResult begin_transform_preview(bool duplicate_selected = false);
  CommandResult
  update_transform_translate_preview(quader::QVec3 delta,
                                     bool texture_lock_enabled = false);
  CommandResult update_transform_translate_preview(
      std::span<const ObjectLocalOffset> offsets,
      bool texture_lock_enabled = false);
  CommandResult
  update_transform_matrix_preview(const quader_poly::Transform3 &transform,
                                  quader::QVec3 pivot,
                                  bool texture_lock_enabled = false,
                                  bool texture_lock_scale_enabled = false);
  CommandResult update_transform_matrix_preview(
      std::span<const ObjectLocalTransform> transforms,
      bool texture_lock_enabled = false,
      bool texture_lock_scale_enabled = false);
  CommandResult
  update_transform_extrude_preview(quader::QVec3 offset,
                                   float closed_edge_ledge_size = 0.0F);
  CommandResult update_transform_extrude_preview(
      std::span<const ObjectLocalOffset> offsets,
      float closed_edge_ledge_size = 0.0F);
  CommandResult
  update_transform_extrude_preview(const quader_poly::Transform3 &transform,
                                   quader::QVec3 pivot);
  CommandResult update_transform_extrude_preview(
      std::span<const ObjectLocalTransform> transforms);
  CommandResult
  update_transform_inset_preview(const quader_poly::Transform3 &transform,
                                 quader::QVec3 pivot);
  CommandResult update_transform_inset_preview(
      std::span<const ObjectLocalTransform> transforms);
  PreviewTransactionFinishResult commit_preview_transaction(
      PreviewTransactionKind expected_kind = PreviewTransactionKind::None);
  PreviewTransactionFinishResult cancel_preview_transaction(
      PreviewTransactionKind expected_kind = PreviewTransactionKind::None);

  void set_selection_mode(SelectionMode mode);
  void clear_selection();
  void set_selected_object_ids(const std::vector<int> &object_ids,
                               int active_id = -1);
  void set_component_selection(int object_id, quader_poly::Selection selection,
                               SelectionMode mode);
  CommandResult create_light(LightType type, quader::QVec3 position,
                             quader::QVec3 direction);
  CommandResult select_light(int light_id);
  CommandResult delete_selected_lights();
  CommandResult set_light_name(int light_id, std::string name);
  CommandResult set_light_enabled(int light_id, bool enabled);
  CommandResult set_light_type(int light_id, LightType type);
  CommandResult set_light_color(int light_id, std::uint32_t color_abgr);
  CommandResult set_light_shadow_mode(int light_id,
                                      LightShadowMode shadow_mode);
  CommandResult set_light_float(int light_id, LightFloatProperty property,
                                float value);
  CommandResult set_light_position(int light_id, quader::QVec3 position);
  CommandResult set_light_direction(int light_id, quader::QVec3 direction);

  [[nodiscard]] bool has_selection() const;
  [[nodiscard]] std::optional<quader::QVec3> selection_center() const;
  [[nodiscard]] Bounds3 document_bounds() const;
  [[nodiscard]] Bounds3 selection_bounds() const;
  [[nodiscard]] ActiveSelection active_selection() const;
  [[nodiscard]] std::vector<int> selected_object_ids() const;
  [[nodiscard]] std::vector<int> selected_light_ids() const;
  [[nodiscard]] std::optional<PickHitRecord>
  pick_nearest(const PickRequest &request) const;
  [[nodiscard]] std::vector<PickHitRecord>
  pick_all(const PickRequest &request) const;
  [[nodiscard]] std::string
  pick_hits_signature(const PickRequest &request) const;

  bool pick_select(
      SelectionEditOperation operation, const quader_poly::Ray &ray,
      quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
      bool allow_component_fallback = true);
  bool pick_select_face(
      SelectionEditOperation operation, const quader_poly::Ray &ray,
      quader_poly::PickOptions pick_options = quader_poly::PickOptions());
  bool pick_select_all_faces(
      SelectionEditOperation operation, const quader_poly::Ray &ray,
      quader_poly::PickOptions pick_options = quader_poly::PickOptions());
  bool pick_select_edge_loop(
      SelectionEditOperation operation, const quader_poly::Ray &ray,
      quader_poly::PickOptions pick_options = quader_poly::PickOptions());
  bool pick_select_all_vertices(
      SelectionEditOperation operation, const quader_poly::Ray &ray,
      quader_poly::PickOptions pick_options = quader_poly::PickOptions());

  CommandResult select_all();
  CommandResult invert_selection();
  CommandResult translate_selected(quader::QVec3 delta,
                                   bool texture_lock_enabled = false);
  CommandResult transform_selected(const quader_poly::Transform3 &transform,
                                   quader::QVec3 pivot,
                                   bool texture_lock_enabled = false,
                                   bool texture_lock_scale_enabled = false);
  CommandResult transform_selected(
      std::span<const ObjectLocalTransform> transforms,
      bool texture_lock_enabled = false,
      bool texture_lock_scale_enabled = false);
  CommandResult snap_selected_vertices_to_active();
  CommandResult snap_selected_edges_to_active();
  CommandResult merge_selected_vertices_to_active();
  CommandResult merge_selected_vertices_to_center();
  CommandResult merge_selected_vertices_by_distance(double tolerance);
  CommandResult merge_selected_vertices_by_distance(
      MergeByDistanceOperationSettings settings);
  CommandResult remove_doubles();
  CommandResult bevel_selected_vertices(float distance);
  CommandResult
  bevel_selected_vertices(BevelVerticesOperationSettings settings);
  CommandResult connect_selected_vertices();
  CommandResult dissolve_selected_vertices();
  CommandResult connect_selected_edges();
  CommandResult split_selected_edges();
  CommandResult dissolve_selected_edges();
  CommandResult merge_selected_edges();
  CommandResult collapse_selected_edges();
  CommandResult fill_hole_from_selected_edges();
  CommandResult
  bevel_selected_edges(const quader_poly::EdgeBevelSettings &settings = {});
  CommandResult bevel_selected_edges(EdgeBevelOperationSettings settings);
  CommandResult bridge_selected_edges(int steps = 1);
  CommandResult bridge_selected_edges(BridgeOperationSettings settings);
  CommandResult delete_selected();
  CommandResult create_box_from_bounds(quader::QVec3 min_bounds,
                                       quader::QVec3 max_bounds);
  CommandResult create_box_from_corners(std::span<const quader::QVec3> corners);
  CommandResult
  create_polygon_from_points(std::span<const quader::QVec3> points,
                             quader::QVec3 face_normal);
  CommandResult create_object_from_document(quader_poly::Document document,
                                            std::string name = "Mesh",
                                            std::uint32_t material_id = 0);
  CommandResult extract_selected_faces();
  CommandResult detach_selected_faces();
  CommandResult bridge_selected_faces(int steps = 1);
  CommandResult bridge_selected_faces(BridgeOperationSettings settings);
  CommandResult thicken_selected_faces(float thickness, bool from_center);
  CommandResult thicken_selected_faces(ThickenOperationSettings settings);
  CommandResult slice_selected_quads(int x_slices = 1, int y_slices = 1);
  CommandResult slice_selected_quads(SliceQuadOperationSettings settings);
  CommandResult combine_selected_faces();
  CommandResult combine_selected_meshes();
  CommandResult collapse_selected_faces();
  CommandResult radial_align_selected();
  CommandResult extrude_selected_elements(quader::QVec3 offset,
                                          float closed_edge_ledge_size = 0.0F);
  CommandResult extrude_selected_elements(
      std::span<const ObjectLocalOffset> offsets,
      float closed_edge_ledge_size = 0.0F);
  CommandResult
  transform_extrude_selected_elements(const quader_poly::Transform3 &transform,
                                      quader::QVec3 pivot);
  CommandResult transform_extrude_selected_elements(
      std::span<const ObjectLocalTransform> transforms);
  CommandResult
  inset_selected_elements(const quader_poly::Transform3 &transform,
                          quader::QVec3 pivot);
  CommandResult
  inset_selected_elements(std::span<const ObjectLocalTransform> transforms);
  CommandResult extrude_selected_faces(float distance);
  CommandResult inset_selected_faces(float amount);
  CommandResult insert_edge_loop(float factor);
  CommandResult insert_edge_loop(int object_id, quader_poly::Edge edge,
                                 float factor);
  bool set_insert_edge_loop_preview_from_pick(
      InsertEdgeLoopToolState &state, SelectionHoverState &hover,
      const quader_poly::Ray &ray, double grid_size,
      quader_poly::PickOptions pick_options, bool snap_enabled,
      bool dragging) const;
  bool update_insert_edge_loop_drag_factor(InsertEdgeLoopToolState &state,
                                           SelectionHoverState &hover,
                                           const quader_poly::Ray &ray,
                                           double grid_size,
                                           bool snap_enabled) const;
  CommandResult commit_knife_tool(KnifeToolState &state);
  CommandResult commit_cut_tool(CutToolState &state);
  CommandResult commit_mirror_tool(MirrorToolState &state);
  CommandResult commit_poly_tool(PolyToolState &state);
  CommandResult knife_segment(int object_id,
                              const quader_poly::KnifePointTarget &previous,
                              const quader_poly::KnifePointTarget &current);
  CommandResult
  knife_stroke(int object_id,
               std::span<const quader_poly::KnifePointTarget> points,
               std::span<const quader_poly::KnifeStrokeSegment> segments);
  CommandResult plane_cut(std::span<const int> object_ids,
                          const quader_poly::PlaneCutRequest &request);
  CommandResult
  mirror_objects_across_plane(std::span<const int> object_ids,
                              std::span<const quader::QVec3> plane_points);
  CommandResult mirror_selected_objects_across_axis(TransformAxis axis);
  CommandResult
  flip_selected_objects_by_orientation(ViewportFlipOrientation orientation);
  CommandResult flip_selected_face_normals();
  CommandResult recalculate_selected_face_normals(bool outside);
  CommandResult shade_selected_faces_smooth();
  CommandResult shade_selected_faces_flat();
  CommandResult harden_selected_edge_normals();
  CommandResult soften_selected_edge_normals();
  CommandResult apply_uv_operation(std::string_view operation,
                                   float shift_world_amount);
  CommandResult assign_material(AssignMaterialOperationSettings settings);
  CommandResult assign_material_to_selection(std::uint32_t material_slot);
  CommandResult assign_material_to_face(int object_id,
                                        quader_poly::ElementId face_id,
                                        std::uint32_t material_slot);
  [[nodiscard]] std::vector<PolygonObject> selected_object_copies() const;
  CommandResult duplicate_selected_objects(quader::QVec3 offset);
  CommandResult
  duplicate_selected_objects(DuplicateMeshesOperationSettings settings);
  CommandResult paste_objects(const std::vector<PolygonObject> &copied_objects,
                              std::optional<quader::QVec3> target_center);

private:
  SessionState state_;
  std::vector<HistoryEntry> undo_stack_;
  std::vector<HistoryEntry> redo_stack_;
  PreviewTransaction preview_transaction_;

  void normalize_active_object();
  void note_selection_changed();
  void note_content_result(const CommandResult &result);
  void restore_recorded_state(SessionState state);
  void begin_preview_transaction(PreviewTransactionKind kind);
  [[nodiscard]] bool
  preview_transaction_active(PreviewTransactionKind kind) const;
  [[nodiscard]] CommandResult finish_preview_begin(CommandResult result);
  [[nodiscard]] CommandResult
  finish_preview_update_from_base(const SessionState &base,
                                  CommandResult result,
                                  CommandResult unchanged_result);
  CommandResult update_bridge_preview_from_base(int steps);
  CommandResult update_edge_bevel_preview_from_base(
      const quader_poly::EdgeBevelSettings &settings);
  CommandResult update_thicken_preview_from_base(float thickness,
                                                 bool from_center);
  CommandResult update_slice_quad_preview_from_base(int x_slices, int y_slices);
  CommandResult update_merge_by_distance_preview_from_base(double tolerance);
  CommandResult update_transform_preview_from_base(CommandResult result);
};

} // namespace quader_editor_core
