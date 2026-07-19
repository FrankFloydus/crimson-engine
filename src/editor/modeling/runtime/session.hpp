////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/interactions/tool_identity.hpp>
#include <editor/modeling/operations/operation_schema.hpp>
#include <editor/modeling/runtime/cow_polygon_document.hpp>
#include <editor/modeling/runtime/selection_types.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>
#include <math/types.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
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
 * Represents a Polygon Object value used by the modeling runtime domain.
 */
struct PolygonObject {
  int id = -1;
  int material_id = 0;
  bool selected = false;
  CowPolygonDocument document;
  quader_poly::Selection selection;
  bool component_edit_target = false;
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
 * Represents a Pick Object Bounds value used by the modeling runtime domain.
 */
struct PickObjectBounds {
  int object_id = -1;
  bool empty = true;
  quader::QVec3 min;
  quader::QVec3 max;
};

/**
 * Carries one participant's affine transform without baking it into shared
 * authored topology.
 */
struct PickObjectTransform {
  int object_id = -1;
  quader::QMat4 world_from_object;
  quader::QMat4 object_from_world;
};

/**
 * Represents a Pick Object Face Geometry value used by the modeling runtime
 * domain.
 */
struct PickObjectFaceGeometry {
  int object_id = -1;
  std::vector<quader_poly::PickFaceTriangle> triangles;

  std::shared_ptr<const std::vector<quader_poly::PickFaceTriangle>>
      shared_triangles;

  [[nodiscard]] std::span<const quader_poly::PickFaceTriangle>
  triangle_span() const noexcept {
    return shared_triangles
               ? std::span<const quader_poly::PickFaceTriangle>(
                     *shared_triangles)
               : std::span<const quader_poly::PickFaceTriangle>(triangles);
  }
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
  int current_object_id = -1;
  int target_object_id = -1;
  quader_poly::KnifePointTarget current_target;
  quader::QVec3 current_point;
  quader::QVec3 face_normal{0.0F, 1.0F, 0.0F};
  std::uint32_t active_stroke_start = 0;
  std::uint32_t previous_point_index = 0;
  std::vector<quader_poly::KnifePointTarget> points;
  std::vector<quader::QVec3> point_positions;
  std::vector<quader_poly::KnifeStrokeSegment> segments;
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
  int target_object_id = -1;
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
  bool edge_loop_select = false;
  bool preserve_existing_component_selection_on_hit = false;
  bool preserve_selection_on_selected_hit = false;
  bool preserve_selection_on_miss = false;
  std::span<const PickObjectBounds> object_bounds;
  std::span<const PickObjectFaceGeometry> face_geometry;
  std::span<const PickObjectTransform> object_transforms;
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
  // Optional borrowed universe for synchronous hot paths. When set, selection
  // resolution reads this value instead of copying the owning fallback above.
  const RectangleSelectionUniverse *universe_view = nullptr;
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
 * Stores one short-lived native projection of SDK modeling state for retained
 * host picking and interaction helpers. It is not a modeling source of truth.
 */
struct SessionState {
  std::vector<PolygonObject> objects;
  int active_object_id = -1;
  SelectionMode selection_mode = SelectionMode::Object;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/** Advances a runtime revision without allowing unsigned wraparound. */
[[nodiscard]] std::uint64_t
advance_session_revision(std::uint64_t revision) noexcept;

SelectionMode sanitize_selection_mode(int mode);
SelectionEditOperation sanitize_selection_operation(int operation);
quader_poly::SelectionMode component_selection_mode(SelectionMode mode);
bool component_selection_has_mode_components(
    const quader_poly::Selection &selection, SelectionMode mode);
bool component_selection_has_any_components(
    const quader_poly::Selection &selection);
bool polygon_selection_matches(const quader_poly::Selection &left,
                               const quader_poly::Selection &right);

int selected_object_index(const std::vector<PolygonObject> &objects,
                          int active_object_id);
int selected_object_id(const std::vector<PolygonObject> &objects,
                       int active_object_id);
std::vector<int> selected_object_ids(const std::vector<PolygonObject> &objects);
void normalize_active_object_id(const std::vector<PolygonObject> &objects,
                                int &active_object_id);
void set_selection_mode(std::vector<PolygonObject> &objects, SelectionMode mode,
                        int &active_object_id, SelectionMode previous_mode);
void clear_selection(std::vector<PolygonObject> &objects,
                     int &active_object_id);
bool has_selection(const std::vector<PolygonObject> &objects,
                   SelectionMode mode, int active_object_id);
std::optional<quader::QVec3>
selection_center(const std::vector<PolygonObject> &objects, SelectionMode mode,
                 int active_object_id);
ActiveSelection active_selection(const std::vector<PolygonObject> &objects,
                                 SelectionMode mode, int active_object_id);
std::optional<PickHitRecord>
pick_nearest(const std::vector<PolygonObject> &objects,
             const PickRequest &request);
std::optional<PickHitRecord>
pick_nearest(const std::vector<PolygonObject> &objects,
             const PickRequest &request,
             std::span<const PickObjectBounds> object_bounds,
             std::span<const PickObjectFaceGeometry> face_geometry,
             std::span<const PickObjectTransform> object_transforms = {});
std::vector<PickHitRecord> pick_all(const std::vector<PolygonObject> &objects,
                                    const PickRequest &request);
std::vector<PickHitRecord>
pick_all(const std::vector<PolygonObject> &objects, const PickRequest &request,
         std::span<const PickObjectBounds> object_bounds,
         std::span<const PickObjectFaceGeometry> face_geometry,
         std::span<const PickObjectTransform> object_transforms = {});
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
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    bool face_plane_hover = false,
    std::span<const PickObjectTransform> object_transforms = {});
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
SelectionOverlayFrame build_object_selection_wire_overlay_frame(
    const PolygonObject &object, const SelectionOverlayStyle &style = {});
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
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {});
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
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {});
bool pick_select_edge_loop(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {});
bool pick_select_all_vertices(
    std::vector<PolygonObject> &objects, SelectionMode &mode,
    SelectionEditOperation operation, const quader_poly::Ray &ray,
    int &active_object_id,
    quader_poly::PickOptions pick_options = quader_poly::PickOptions(),
    std::span<const PickObjectBounds> object_bounds = {},
    std::span<const PickObjectFaceGeometry> face_geometry = {},
    std::span<const PickObjectTransform> object_transforms = {});

Bounds3 document_bounds(const std::vector<PolygonObject> &objects);
Bounds3 selection_bounds(const std::vector<PolygonObject> &objects,
                         SelectionMode mode);

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
void clear_knife_hover(KnifeToolState &state, SelectionHoverState &hover);
void rebuild_knife_overlay(KnifeToolState &state);
bool update_knife_preview_from_pick(
    EditorSession &session, KnifeToolState &state, SelectionHoverState &hover,
    const quader_poly::Ray &ray, double grid_size, bool snap_enabled,
    bool constrain_axis,
    int target_object_id = -1);
bool update_knife_preview_from_hit(
    EditorSession &session, KnifeToolState &state, SelectionHoverState &hover,
    const PickHitRecord &hit, double grid_size, bool snap_enabled,
    bool constrain_axis);
bool accept_knife_current_point(KnifeToolState &state, double grid_size);
KnifeSegmentPreview
build_knife_segment_preview(const std::vector<PolygonObject> &objects,
                            int object_id,
                            const quader_poly::KnifePointTarget &previous,
                            const quader_poly::KnifePointTarget &current);
/**
 * Owns Editor Session behavior within the modeling runtime domain.
 */
class EditorSession {
public:
  explicit EditorSession(std::vector<PolygonObject> objects);

  [[nodiscard]] const SessionState &state() const;
  [[nodiscard]] SessionState &state();
  [[nodiscard]] const std::vector<PolygonObject> &objects() const;
  [[nodiscard]] std::vector<PolygonObject> &objects();

  [[nodiscard]] int active_object_id() const;
  [[nodiscard]] SelectionMode selection_mode() const;

  void set_selection_mode(SelectionMode mode);
  void clear_selection();
  void set_selected_object_ids(const std::vector<int> &object_ids,
                               int active_id = -1);
  void set_component_selection(int object_id, quader_poly::Selection selection,
                               SelectionMode mode);

  [[nodiscard]] bool has_selection() const;
  [[nodiscard]] std::optional<quader::QVec3> selection_center() const;
  [[nodiscard]] Bounds3 document_bounds() const;
  [[nodiscard]] Bounds3 selection_bounds() const;
  [[nodiscard]] ActiveSelection active_selection() const;
  [[nodiscard]] std::vector<int> selected_object_ids() const;
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

  bool set_insert_edge_loop_preview_from_pick(
      InsertEdgeLoopToolState &state, SelectionHoverState &hover,
      const quader_poly::Ray &ray, double grid_size,
      quader_poly::PickOptions pick_options, bool snap_enabled,
      bool dragging, int target_object_id = -1) const;
  bool set_insert_edge_loop_preview_from_hit(
      InsertEdgeLoopToolState &state, SelectionHoverState &hover,
      const PickHitRecord &hit, std::optional<float> resolved_factor,
      bool dragging) const;
  bool update_insert_edge_loop_drag_factor(InsertEdgeLoopToolState &state,
                                           SelectionHoverState &hover,
                                           const quader_poly::Ray &ray,
                                           double grid_size,
                                           bool snap_enabled) const;

private:
  SessionState state_;

  void normalize_active_object();
  void note_selection_changed();
};

} // namespace quader_editor_core
