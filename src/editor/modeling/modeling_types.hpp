////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "editor/modeling/radial_array_mode.hpp"
#include "editor/modeling/modeling_preview_types.hpp"
#include "editor/modeling/modeling_result_types.hpp"
#include "math/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {

/**
 * Identifies the transient scene-local definition projection used while an
 * occurrence is being edited. This is presentation/input state, not document
 * state.
 */
struct ModelingInstanceEditScope final {
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;

  [[nodiscard]] bool active() const noexcept {
    return occurrence != kInvalidEntityId && definition != kInvalidEntityId;
  }

  friend bool operator==(const ModelingInstanceEditScope &,
                         const ModelingInstanceEditScope &) = default;
};

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
  DefaultEdgeNormals,
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
 * Enumerates the coordinate space used by interactive transforms.
 */
enum class ModelingTransformSpace {
  Global,
  Local,
};

/**
 * Enumerates ModelingViewportTool values used by the modeling layer.
 */
enum class ModelingViewportTool {
  Select,
  Move,
  Extrude,
  Rotate,
  Scale,
  Box,
  Cylinder,
  Sphere,
  InsertEdgeLoop,
  Knife,
  Cut,
  Poly,
  Mirror,
  LassoSelect,
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
 * Identifies topology-aware component selection commands exposed by the
 * modeling application service.
 */
enum class ModelingComponentSelectionCommand {
  SelectEdgeDirection,
  SelectEdgeLoops,
  SelectEdgeRing,
  SelectFacesWithSameMaterial,
  SelectCoplanarFaces,
  SelectFaceNormal,
  Grow,
  Shrink,
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

/** Identifies an editor-owned component selection projected with mesh selection. */
enum class ModelingSubSelectionKind {
  None,
  CurveControl,
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
  bool affine_model_space = false;
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
  double vertex_screen_radius = 0.0;
  double edge_screen_radius = 0.0;
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
  QVec2 footprint_size = {};
  QVec2 signed_footprint_size = {};
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
  MaterialReference material;
  // Optional scene-local placement for the bounds-authored document.
  std::optional<ModelingTransform3> initial_scene_transform;
};

/**
 * Stores the Modeling Create Mesh Request data contract used by the modeling
 * domain.
 */
struct ModelingCreateMeshRequest {
  quader_poly::Document document;
  std::string name = "Mesh";
  std::uint32_t material_slot = 0;
  MaterialReference material;
  // Optional scene-local placement for already-local document geometry. Most
  // callers omit this and retain the established identity projection.
  std::optional<ModelingTransform3> initial_scene_transform;
  // Generated primitives opt in to the construction world's texture chart.
  // Imported/custom documents keep their authored sampling by default.
  bool world_texture_projection = false;
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

/** Selects a topology expansion for an edge click. */
enum class ModelingEdgeSelectionTraversal {
  None,
  Loop,
  Ring,
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
  /// Keeps the mesh selected as a component edit target even when this
  /// request intentionally contains no selected components.
  bool retain_component_edit_target = false;
};

/**
 * Identifies the pickable component universe that may be toggled by one
 * selection inversion. Components outside this universe retain their current
 * selection state.
 */
struct ModelingComponentSelectionUniverse {
  int modeling_object_id = -1;
  ModelingSelectionMode mode = ModelingSelectionMode::Vertex;
  std::vector<int> vertices;
  std::vector<std::pair<int, int>> edges;
  std::vector<int> faces;
};

/** Applies full click policy to one component hit resolved by another owner. */
struct ModelingResolvedComponentSelectionRequest {
  int modeling_object_id = -1;
  ModelingSelectionMode mode = ModelingSelectionMode::Vertex;
  ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
  int vertex = -1;
  std::pair<int, int> edge = {-1, -1};
  int face = -1;
  ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
  int click_count = 1;
  ModelingEdgeSelectionTraversal edge_traversal =
      ModelingEdgeSelectionTraversal::None;
  bool preserve_selection_on_selected_hit = false;
  bool preserve_existing_component_selection_on_hit = false;
};

/** Applies component-mode policy to a mesh body hit with no component hit. */
struct ModelingResolvedComponentBodySelectionRequest {
  int modeling_object_id = -1;
  EntityId source_occurrence = kInvalidEntityId;
  ModelingSelectionMode mode = ModelingSelectionMode::Vertex;
  ModelingSelectionEdit edit = ModelingSelectionEdit::Replace;
  bool preserve_selection_on_selected_hit = false;
  bool preserve_existing_component_selection_on_hit = false;
};

/**
 * Stores the Modeling Direct Hover Request data contract used by the modeling
 * domain.
 */
struct ModelingDirectHoverRequest {
  int modeling_object_id = -1;
  int candidate_object_id = -1;
  ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
  int vertex_id = -1;
  std::pair<int, int> edge = {-1, -1};
  int face_id = -1;
  bool selected_modifier_pressed = false;
  bool face_plane_hover = false;
};

/// Optional viewport aperture; positions and radii share physical-pixel units.
struct ModelingScreenPickContext {
  ModelingViewportCamera camera;
  QVec2 screen_position = {};
  double vertex_radius_pixels = 0.0;
  double edge_radius_pixels = 0.0;
  double near_plane = 0.01;
  double far_plane = 1000000.0;
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
  bool select_backfaces = true;
  bool cycle_depth = false;
  double cycle_origin_tolerance = 0.0;
  std::optional<ModelingScreenPickContext> screen_context;

  [[nodiscard]] ModelingEdgeSelectionTraversal
  edge_traversal_requested(ModelingSelectionMode mode) const noexcept {
    if (mode != ModelingSelectionMode::Edge || !alt_pressed) {
      return ModelingEdgeSelectionTraversal::None;
    }
    return control_pressed || command_pressed
               ? ModelingEdgeSelectionTraversal::Ring
               : ModelingEdgeSelectionTraversal::Loop;
  }

  [[nodiscard]] bool depth_cycle_requested(ModelingSelectionMode mode) const noexcept {
    return mode == ModelingSelectionMode::Object && cycle_depth &&
           click_count > 0 && !alt_pressed;
  }
};

/// Identifies one live polygon face for a host-coordinated material rewrite.
struct ModelingFaceMaterialReplacementTarget final {
  int modeling_object_id = kInvalidModelingObjectId;
  int face_id = quader_poly::kInvalidElementId;

  [[nodiscard]] bool operator==(
      const ModelingFaceMaterialReplacementTarget &) const = default;
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
  bool select_backfaces = true;
};

/** Read-only lasso projection request; the ordered path is caller-owned. */
struct ModelingLassoSelectRequest {
  ModelingViewportCamera camera;
  std::span<const QVec2> points;
  bool partial_intersection = false;
  bool select_backfaces = true;
  double near_plane = 0.01;
  double far_plane = 1000000.0;
};

/** One actual projected geometry point that caused a rectangle hit. */
struct ModelingSelectionVisibilityWitness {
  QVec2 screen_position;
  QVec3 world_position;

  [[nodiscard]] bool operator==(
      const ModelingSelectionVisibilityWitness &other) const noexcept {
    return screen_position.x == other.screen_position.x &&
           screen_position.y == other.screen_position.y &&
           world_position.x == other.world_position.x &&
           world_position.y == other.world_position.y &&
           world_position.z == other.world_position.z;
  }
};

/** One nonmutating polygon selection candidate with exact pick metrics. */
struct ModelingSelectionHit {
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingActiveSelectionKind kind = ModelingActiveSelectionKind::None;
  int vertex_id = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face_id = -1;
  double ray_distance = 0.0;
  double pick_distance = 0.0;
  std::optional<QVec3> world_position;
  std::vector<ModelingSelectionVisibilityWitness> visibility_witnesses;
  /// Exact component rectangle match whose publication needs no depth metrics.
  bool rectangle_target_only = false;
  std::string signature;
  /// Exact visible occurrence whose projected geometry produced this hit.
  /// Standalone meshes use their own entity; instance editing uses the active
  /// occurrence while `entity` remains the authored geometry member.
  EntityId source_occurrence = kInvalidEntityId;
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
  bool select_backfaces = true;
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
  int candidate_object_id = -1;
  bool selected_modifier_pressed = false;
  bool face_plane_hover = false;
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
  std::optional<bool> selected_faces_have_open_perimeter;
  std::optional<bool> combinable_selected_faces;
  std::optional<bool> bridge_face_pair;
  std::optional<bool> bridge_edge_pair;
  std::optional<bool> selected_edges_belong_to_faces;
  std::optional<bool> selected_edges_are_interior;
  std::optional<bool> three_selected_open_boundary_edges;
  std::optional<bool> selected_vertices_belong_to_faces;
  std::optional<bool> selected_vertices_share_face;
  std::optional<bool> whole_mesh_closed;
  std::optional<bool> whole_mesh_has_open_perimeter;
  ModelingSubSelectionKind sub_selection_kind =
      ModelingSubSelectionKind::None;
  std::uint32_t selected_sub_element_count = 0;
  std::uint64_t sub_selection_revision = 0;
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
  bool suppress_persistent_selection = false;
  // Tool-owned topology context is retained with the persistent overlay, not
  // rebuilt from the volatile selection hover stream.
  int persistent_source_wire_object_id = -1;
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
 * Represents a Modeling Repeat Transform value used by the modeling domain.
 */
struct ModelingRepeatTransform {
  bool active = false;
  ModelingTransformTool tool = ModelingTransformTool::Move;
  ModelingTransformOperation operation = ModelingTransformOperation::Transform;
  ModelingTransformAxis axis = ModelingTransformAxis::None;
  ModelingTransformSpace space = ModelingTransformSpace::Global;
  ModelingTransform3 orientation = {};
  bool duplicate_selected = false;
  bool move_auto_merge_vertices = false;
  QVec3 pivot = {};
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
  bool knife_has_active_stroke = false;
  bool knife_has_current = false;
  bool knife_current_closes_stroke = false;
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
  bool poly_current_closes_stroke = false;
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
  Move,
  LassoSelect,
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
  // Cached accepted Bevel saturation; absent until this source has reached it.
  std::optional<double> edge_bevel_maximum_width;
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
  bool move_auto_merge_vertices = true;
  bool lasso_select_partial_intersection = false;

  [[nodiscard]] bool
  operator==(const ModelingInteractionSettings &) const = default;
};

/**
 * Stores the Modeling Transform Drag Begin Request data contract used by the
 * modeling domain.
 */
struct ModelingTransformDragBeginRequest {
  ModelingTransformTool tool = ModelingTransformTool::Move;
  ModelingTransformOperation operation = ModelingTransformOperation::Transform;
  ModelingTransformAxis axis = ModelingTransformAxis::None;
  ModelingTransformSpace space = ModelingTransformSpace::Global;
  ModelingTransform3 orientation = {};
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
  double rotation_snap_degrees = 15.0;
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
  double rotation_snap_degrees = 15.0;
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
 * Identifies one live native modeling source for a transformed array commit.
 */
struct ModelingArraySource {
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = -1;
};

/**
 * Describes one atomic, source-inclusive native linear array commit.
 */
struct ModelingLinearArrayRequest {
  std::vector<ModelingArraySource> sources;
  QVec3 span_world = {};
  std::uint32_t count = 4;
};

/**
 * Returns one source and its source-inclusive start-to-end element order.
 */
struct ModelingLinearArraySourceResult {
  ModelingObjectRecord source;
  std::vector<ModelingObjectRecord> elements;
};

/**
 * Returns linear-array groups in the request's source order.
 */
struct ModelingLinearArrayResult {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::vector<ModelingLinearArraySourceResult> source_arrays;
};

/**
 * Describes one atomic, source-inclusive native radial array commit.
 */
struct ModelingRadialArrayRequest {
  std::vector<ModelingArraySource> sources;
  QVec3 center_world = {};
  QVec3 plane_normal_world = {0.0f, 1.0f, 0.0f};
  double degrees = 360.0;
  std::uint32_t copies = 8u;
  std::uint32_t repeats = 1u;
  double repeat_spacing = 1.0;
  RadialArrayMode mode = RadialArrayMode::Total;
};

/**
 * Returns one source and its source-inclusive ring/angle element order.
 */
struct ModelingRadialArraySourceResult {
  ModelingObjectRecord source;
  std::vector<ModelingObjectRecord> elements;
};

/**
 * Returns radial-array groups in the request's source order.
 */
struct ModelingRadialArrayResult {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::vector<ModelingRadialArraySourceResult> source_arrays;
};

} // namespace quader
