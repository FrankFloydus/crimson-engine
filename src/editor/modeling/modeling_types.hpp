////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <mesh/polygon/document.hpp>

namespace quader {

/**
 * Enumerates ModelingCommandId values used by the modeling layer.
 */
enum class ModelingCommandId {
  ActivateBoxTool,
  CreateBoxFromBounds,
  SelectAll,
  InvertSelection,
  ClearSelection,
  DeleteSelection,
  ExecuteInstantOperation,
};

/**
 * Enumerates ModelingInstantOperationId values used by the modeling layer.
 */
enum class ModelingInstantOperationId {
  DeleteSelection,
  DuplicateMeshes,
  CombineMeshes,
  ExtractFaces,
  DetachFaces,
  Bridge,
  SnapVerticesToActive,
  ConnectVertices,
  DissolveVertices,
  BevelVertices,
  MergeVerticesToActive,
  MergeVerticesToCenter,
  RemoveDoubles,
  SnapEdgesToActive,
  ConnectEdges,
  SplitEdges,
  DissolveEdges,
  MergeEdges,
  CollapseEdges,
  FillHole,
  CombineFaces,
  CollapseFaces,
  RadialAlign,
  FlipFaceNormals,
  RecalculateFaceNormals,
  ShadeFacesSmooth,
  ShadeFacesFlat,
  HardenEdgeNormals,
  SoftenEdgeNormals,
};

/**
 * Enumerates ModelingMirrorAxis values used by the modeling layer.
 */
enum class ModelingMirrorAxis {
  X,
  Y,
  Z,
};

/**
 * Enumerates ModelingFlipOrientation values used by the modeling layer.
 */
enum class ModelingFlipOrientation {
  Horizontal,
  Vertical,
};

/**
 * Enumerates ModelingTransformTool values used by the modeling layer.
 */
enum class ModelingTransformTool {
  Move,
  Rotate,
  Scale,
};

/**
 * Enumerates ModelingViewportTool values used by the modeling layer.
 */
enum class ModelingViewportTool {
  Select,
  Move,
  Extend,
  Extrude,
  Rotate,
  Scale,
  Box,
  InsertEdgeLoop,
  Knife,
  Cut,
  Poly,
  Mirror,
};

/**
 * Enumerates ModelingCutMode values used by the modeling layer.
 */
enum class ModelingCutMode {
  DiscardLeft,
  DiscardRight,
  KeepBoth,
};

/**
 * Enumerates the local mesh axis used by the Half-Cut operation.
 */
enum class ModelingHalfCutAxis {
  X = 0,
  Y = 1,
  Z = 2,
};

/**
 * Enumerates ModelingTransformOperation values used by the modeling layer.
 */
enum class ModelingTransformOperation {
  Transform,
  Extrude,
  Inset,
};

/**
 * Enumerates ModelingTransformAxis values used by the modeling layer.
 */
enum class ModelingTransformAxis {
  None,
  X,
  Y,
  Z,
  XY,
  XZ,
  YZ,
  All,
};

/**
 * Stores the Modeling Command Descriptor data contract used by the modeling
 * domain.
 */
struct ModelingCommandDescriptor {
  ModelingCommandId command = ModelingCommandId::ActivateBoxTool;
  std::string id;
  std::string label;
  std::string shortcut;
  bool viewport_scoped = true;
  bool rebuilds_mesh_on_success = false;
};

/**
 * Stores the Modeling Instant Operation Descriptor data contract used by the
 * modeling domain.
 */
struct ModelingInstantOperationDescriptor {
  ModelingInstantOperationId operation =
      ModelingInstantOperationId::DuplicateMeshes;
  std::string id;
  std::string label;
  bool enabled = false;
  bool rebuilds_mesh_on_success = true;
};

/**
 * Represents a Modeling Bounds value used by the modeling domain.
 */
struct ModelingBounds {
  QVec3 min;
  QVec3 max;
};

/**
 * Enumerates ModelingBoxDragState values used by the modeling layer.
 */
enum class ModelingBoxDragState {
  Idle,
  Footprint,
  Height,
};

/**
 * Enumerates ModelingSelectionMode values used by the modeling layer.
 */
enum class ModelingSelectionMode {
  Vertex,
  Edge,
  Face,
  Object,
  Group,
};

/**
 * Enumerates ModelingActiveSelectionKind values used by the modeling layer.
 */
enum class ModelingActiveSelectionKind {
  None,
  Object,
  Vertex,
  Edge,
  Face,
  Light,
};

/**
 * Represents a Modeling Ray value used by the modeling domain.
 */
struct ModelingRay {
  QVec3 origin;
  QVec3 direction;
};

/**
 * Represents a Modeling Viewport Camera value used by the modeling domain.
 */
struct ModelingViewportCamera {
  bool orthographic = false;
  QVec3 origin = {};
  QVec3 x_axis = {1.0f, 0.0f, 0.0f};
  QVec3 y_axis = {0.0f, 1.0f, 0.0f};
  QVec3 z_axis = {0.0f, 0.0f, 1.0f};
  QVec2 viewport_size = {1.0f, 1.0f};
  double orthographic_size = 24.0;
  double fov_degrees = 60.0;
};

/**
 * Represents a Modeling Transform3 value used by the modeling domain.
 */
struct ModelingTransform3 {
  QVec3 x_axis = {1.0f, 0.0f, 0.0f};
  QVec3 y_axis = {0.0f, 1.0f, 0.0f};
  QVec3 z_axis = {0.0f, 0.0f, 1.0f};
  QVec3 origin = {};
};

/**
 * Stores the Modeling Box Tool Event data contract used by the modeling domain.
 */
struct ModelingBoxToolEvent {
  QVec2 screen_position;
  ModelingRay ray;
  double grid_size = 1.0;
  bool has_camera = false;
  ModelingViewportCamera camera;
  bool snap_enabled = true;
};

/**
 * Stores the Modeling Tool Ray Event data contract used by the modeling domain.
 */
struct ModelingToolRayEvent {
  QVec2 screen_position;
  ModelingRay ray;
  ModelingViewportCamera camera;
  double grid_size = 1.0;
  double vertex_radius = 0.3;
  double edge_radius = 0.2;
  bool snap_enabled = true;
  bool constrain_axis = false;
  bool dragging = false;
};

/**
 * Represents a Modeling Box Preview value used by the modeling domain.
 */
struct ModelingBoxPreview {
  bool ok = false;
  bool valid = false;
  ModelingBoxDragState drag_state = ModelingBoxDragState::Idle;
  std::string message;
  std::array<QVec3, 4> footprint = {};
  std::size_t footprint_count = 0;
  std::array<QVec3, 8> corners = {};
  std::size_t corner_count = 0;
  QVec3 normal = {0.0f, 1.0f, 0.0f};
  double distance = 0.0;
  ModelingBounds bounds{};
  bool bounds_empty = true;
};

/**
 * Stores the Modeling Create Box Request data contract used by the modeling
 * domain.
 */
struct ModelingCreateBoxRequest {
  ModelingBounds bounds{
      .min = {-1.0f, -1.0f, -1.0f},
      .max = {1.0f, 1.0f, 1.0f},
  };
  std::string name = "Box";
};

/**
 * Stores the Modeling Create Mesh Request data contract used by the modeling
 * domain.
 */
struct ModelingCreateMeshRequest {
  quader_poly::Document document;
  std::string name = "Mesh";
  std::uint32_t material_slot = 0;
};

/**
 * Enumerates ModelingSelectionEdit values used by the modeling layer.
 */
enum class ModelingSelectionEdit {
  Replace,
  Add,
  Remove,
  Toggle,
};

/**
 * Enumerates ModelingBoxSelectionRule values used by the modeling layer.
 */
enum class ModelingBoxSelectionRule {
  DragDirection,
  Contain,
  Crossing,
};

/**
 * Stores the Modeling Component Selection Request data contract used by the
 * modeling domain.
 */
struct ModelingComponentSelectionRequest {
  int modeling_object_id = -1;
  ModelingSelectionMode mode = ModelingSelectionMode::Vertex;
  std::vector<int> vertices;
  std::vector<std::pair<int, int>> edges;
  std::vector<int> faces;
  ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
  ModelingActiveSelectionKind active_kind = ModelingActiveSelectionKind::None;
  int active_vertex = -1;
  std::pair<int, int> active_edge = {-1, -1};
  int active_face = -1;
};

/**
 * Stores the Modeling Direct Hover Request data contract used by the modeling
 * domain.
 */
struct ModelingDirectHoverRequest {
  int modeling_object_id = -1;
  ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
  int vertex_id = -1;
  std::pair<int, int> edge = {-1, -1};
  int face_id = -1;
  bool selected_modifier_pressed = false;
  bool face_plane_hover = false;
};

/**
 * Stores the Modeling Pick Select Request data contract used by the modeling
 * domain.
 */
struct ModelingPickSelectRequest {
  ModelingRay ray;
  int click_count = 1;
  bool control_pressed = false;
  bool shift_pressed = false;
  bool command_pressed = false;
  bool alt_pressed = false;
  bool preserve_selection_on_miss = false;
  bool preserve_selection_on_selected_hit = false;
  bool preserve_existing_component_selection_on_hit = false;
  double vertex_radius = 0.3;
  double edge_radius = 0.2;
};

/**
 * Stores the Modeling Box Select Request data contract used by the modeling
 * domain.
 */
struct ModelingBoxSelectRequest {
  ModelingViewportCamera camera;
  QVec2 start = {};
  QVec2 end = {};
  ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
  ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::DragDirection;
};

/**
 * Stores the Modeling Hover Request data contract used by the modeling domain.
 */
struct ModelingHoverRequest {
  ModelingRay ray;
  bool selected_modifier_pressed = false;
  bool face_plane_hover = false;
  double vertex_radius = 0.3;
  double edge_radius = 0.2;
};

/**
 * Represents a Modeling Hover Target value used by the modeling domain.
 */
struct ModelingHoverTarget {
  ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
  int object_id = -1;
  int vertex_id = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face_id = -1;
};

/**
 * Represents a Modeling Hover Summary value used by the modeling domain.
 */
struct ModelingHoverSummary {
  bool changed = false;
  ModelingHoverTarget target;
  bool target_selected = false;
};

/**
 * Represents a Modeling Selection Summary value used by the modeling domain.
 */
struct ModelingSelectionSummary {
  ModelingSelectionMode mode = ModelingSelectionMode::Object;
  bool has_selection = false;
  ModelingActiveSelectionKind active_kind = ModelingActiveSelectionKind::None;
  int active_object_id = -1;
  int active_vertex_id = -1;
  int active_edge_a = -1;
  int active_edge_b = -1;
  int active_face_id = -1;
  std::uint32_t selected_object_count = 0;
  std::uint32_t selected_vertex_count = 0;
  std::uint32_t selected_edge_count = 0;
  std::uint32_t selected_face_count = 0;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Stores one selected vertex snapshot for debug selection export.
 */
struct ModelingSelectionExportVertex {
  quader_poly::ElementId id = quader_poly::kInvalidElementId;
  QVec3 position = {};
};

/**
 * Stores one selected edge snapshot for debug selection export.
 */
struct ModelingSelectionExportEdge {
  quader_poly::ElementId a = quader_poly::kInvalidElementId;
  quader_poly::ElementId b = quader_poly::kInvalidElementId;
  std::optional<QVec3> a_position;
  std::optional<QVec3> b_position;
};

/**
 * Stores one selected face snapshot for debug selection export.
 */
struct ModelingSelectionExportFace {
  quader_poly::ElementId id = quader_poly::kInvalidElementId;
  std::vector<quader_poly::ElementId> vertices;
};

/**
 * Stores the selected subset of one runtime modeling object.
 */
struct ModelingSelectionExportObject {
  int modeling_object_id = -1;
  EntityId entity = kInvalidEntityId;
  std::string entity_uuid;
  std::string name;
  bool selected = false;
  quader_poly::Selection selection;
  std::vector<ModelingSelectionExportVertex> selected_vertices;
  std::vector<ModelingSelectionExportEdge> selected_edges;
  std::vector<ModelingSelectionExportFace> selected_faces;
};

/**
 * Stores a bounded debug snapshot of the current modeling selection.
 */
struct ModelingSelectionExport {
  ModelingSelectionSummary summary;
  std::vector<ModelingSelectionExportObject> objects;
};

/**
 * Stores selection overlay projection options for callers that have
 * dedicated renderers for some
 * selection feedback.
 */
struct ModelingSelectionOverlayOptions {
  bool suppress_persistent_object_selection = false;
};

/**
 * Stores the Modeling Object Record data contract used by the modeling
 * domain.
 */
struct ModelingObjectRecord {
  int modeling_object_id = -1;
  EntityId entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  std::string name;
};

/**
 * Stores the Modeling Command Result data contract used by the modeling domain.
 */
struct ModelingCommandResult {
  bool ok = false;
  bool changed = false;
  std::string message;
};

/**
 * Represents a Modeling Repeat Transform value used by the modeling domain.
 */
struct ModelingRepeatTransform {
  bool active = false;
  ModelingTransformTool tool = ModelingTransformTool::Move;
  ModelingTransformOperation operation = ModelingTransformOperation::Transform;
  ModelingTransformAxis axis = ModelingTransformAxis::None;
  bool duplicate_selected = false;
  QVec3 move_delta = {};
  double rotate_angle_degrees = 0.0;
  QVec3 scale = {1.0f, 1.0f, 1.0f};
};

/**
 * Represents a Modeling Tool Status value used by the modeling domain.
 */
struct ModelingToolStatus {
  ModelingViewportTool active_tool = ModelingViewportTool::Select;
  bool modal_tool_owns_viewport = false;
  bool insert_edge_loop_active = false;
  bool insert_edge_loop_dragging = false;
  bool insert_edge_loop_has_preview = false;
  bool knife_active = false;
  bool knife_has_current = false;
  bool knife_can_commit = false;
  bool cut_active = false;
  bool cut_can_preview = false;
  bool cut_preview_valid = false;
  bool cut_dragging = false;
  bool cut_dragging_new_point = false;
  bool cut_dragging_line = false;
  int cut_dragging_point_index = -1;
  int cut_point_count = 0;
  int cut_captured_point_count = 0;
  bool cut_has_line = false;
  std::array<QVec3, 3> cut_points = {};
  ModelingCutMode cut_mode = ModelingCutMode::KeepBoth;
  bool mirror_active = false;
  bool mirror_can_preview = false;
  bool mirror_preview_valid = false;
  bool mirror_dragging = false;
  bool mirror_dragging_new_point = false;
  bool mirror_dragging_line = false;
  int mirror_dragging_point_index = -1;
  int mirror_point_count = 0;
  int mirror_captured_point_count = 0;
  bool mirror_has_line = false;
  std::array<QVec3, 3> mirror_points = {};
  bool poly_active = false;
  bool poly_closed = false;
  bool poly_has_current = false;
  bool poly_can_commit = false;
  int poly_point_count = 0;
};

/**
 * Enumerates live status message kinds for status-bar operation feedback.

 */
enum class ModelingLiveStatusKind {
  None,
  MergeByDistanceMergedVertices,
};

/**
 * Represents a live status value used by editor UI surfaces.
 */
struct ModelingLiveStatus {
  ModelingLiveStatusKind kind = ModelingLiveStatusKind::None;
  std::uint32_t count = 0;
};

/**
 * Enumerates ModelingToolSettingsTargetKind values used by the modeling
 * layer.
 */
enum class ModelingToolSettingsTargetKind {
  None,
  Cut,
  Mirror,
  Bridge,
  EdgeBevel,
  Thicken,
  SliceQuad,
  MergeByDistance,
  HalfCut,
};

/**
 * Represents a Modeling Tool Settings Target value used by the modeling domain.
 */
struct ModelingToolSettingsTarget {
  ModelingToolSettingsTargetKind kind = ModelingToolSettingsTargetKind::None;
  bool active = false;
  bool operation_preview_active = false;
  std::string label;
  std::string action_id;
  std::string operation_id;
};

/**
 * Represents a Modeling Interaction Settings value used by the modeling domain.
 */
struct ModelingInteractionSettings {
  int bridge_steps = 1;
  double edge_bevel_width = 1.0;
  double edge_bevel_profile = 0.5;
  int edge_bevel_segments = 1;
  double thicken_thickness = 0.25;
  bool thicken_from_center = false;
  int slice_quad_x_slices = 1;
  int slice_quad_y_slices = 1;
  double merge_by_distance_tolerance = 0.125;
  ModelingHalfCutAxis half_cut_axis = ModelingHalfCutAxis::X;
  bool half_cut_separate_parts = false;
  bool half_cut_fill_parts = true;
  ModelingCutMode cut_mode = ModelingCutMode::KeepBoth;
};

/**
 * Represents a Modeling Tool Preview Mesh value used by the modeling domain.
 */
struct ModelingToolPreviewMesh {
  EntityId source_entity = kInvalidEntityId;
  AssetId material = kInvalidAssetId;
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
};

/**
 * Stores the Modeling Transform Drag Begin Request data contract used by the
 * modeling domain.
 */
struct ModelingTransformDragBeginRequest {
  ModelingTransformTool tool = ModelingTransformTool::Move;
  ModelingTransformOperation operation = ModelingTransformOperation::Transform;
  ModelingTransformAxis axis = ModelingTransformAxis::None;
  QVec3 pivot = {};
  QVec2 screen_position = {};
  QVec2 screen_pivot = {};
  QVec2 screen_tip = {};
  bool has_world_direction = false;
  QVec3 world_direction = {};
  ModelingRay ray;
  ModelingViewportCamera camera;
  double grid_size = 1.0;
  double vertex_snap_radius = 0.3;
  bool grid_snap_enabled = true;
  bool precision = false;
  bool duplicate_selected = false;
};

/**
 * Stores the Modeling Transform Drag Update Request data contract used by the
 * modeling domain.
 */
struct ModelingTransformDragUpdateRequest {
  QVec2 screen_position = {};
  ModelingRay ray;
  ModelingViewportCamera camera;
  double grid_size = 1.0;
  double vertex_snap_radius = 0.3;
  bool grid_snap_enabled = true;
  bool precision = false;
};

/**
 * Represents a Modeling Transform Drag Preview value used by the modeling
 * domain.
 */
struct ModelingTransformDragPreview {
  bool ok = false;
  bool active = false;
  bool changed = false;
  bool preview_driven = false;
  std::string message;
  ModelingTransformTool tool = ModelingTransformTool::Move;
  ModelingTransformOperation operation = ModelingTransformOperation::Transform;
  ModelingTransformAxis axis = ModelingTransformAxis::None;
  QVec3 pivot = {};
  QVec3 move_delta = {};
  double rotate_angle_degrees = 0.0;
  QVec3 scale = {1.0f, 1.0f, 1.0f};
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  bool scene_transform = false;
  bool has_scene_preview_transform = false;
  ModelingTransform3 scene_preview_transform = {};
};

/**
 * Stores the Modeling Create Box Result data contract used by the modeling
 * domain.
 */
struct ModelingCreateBoxResult {
  bool ok = false;
  bool changed = false;
  std::string message;
  ModelingObjectRecord object;
};

/**
 * Describes one native world-space linear array commit.
 */
struct ModelingLinearArrayRequest {
  EntityId source_entity = kInvalidEntityId;
  int source_modeling_object_id = -1;
  QVec3 endpoint_world = {};
  std::uint32_t count = 4;
};

/**
 * Returns source and created objects in start-to-end array order.
 */
struct ModelingLinearArrayResult {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::vector<ModelingObjectRecord> elements;
};

} // namespace quader
