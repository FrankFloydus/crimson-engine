////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;
class QuaderMeshHandle;
class QuaderMeshVertices;
class QuaderMeshEdges;
class QuaderMeshFaces;
class QuaderMeshTransform;
class QuaderMeshMaterials;
class QuaderPreviewHandle;
class QuaderModelingSelection;
class QuaderObjectSelection;
class QuaderVertexSelection;
class QuaderEdgeSelection;
class QuaderFaceSelection;

/**
 * Names one edge for native public modeling component APIs.
 */
struct QuaderEdgeRef {
  QuaderElementId a = kInvalidQuaderElementId;
  QuaderElementId b = kInvalidQuaderElementId;
};

/**
 * Configures native public edge bevel previews.
 */
struct QuaderEdgeBevelArgs {
  double width = 1.0;
  double profile = 0.5;
  int segments = 1;
};

/// Selects the texture axis that follows the selected edge guides.
enum class QuaderUvPeelAxis { U, V };

/// Scene-local peel settings; changing them does not edit the document.
struct QuaderUvPeelOptions final {
  QuaderUvPeelAxis axis = QuaderUvPeelAxis::U;
  bool world_space = true;
  double u_repeat = 1.0;
  double v_repeat = 1.0;
  double u_offset = 0.0;
  double v_offset = 0.0;

  friend bool operator==(const QuaderUvPeelOptions &,
                         const QuaderUvPeelOptions &) = default;
};

/// Pins captured settings to the edge selection and document that supplied them.
struct QuaderUvPeelRequest final {
  QuaderUvPeelOptions options;
  std::uint64_t selection_token = 0;
};

/// Localizable causes that disable UV Peel without hiding selected guides.
enum class QuaderUvPeelBlockReason {
  None,
  NoSelection,
  PreviewActive,
  TargetUnavailable,
};

/// Cheap retained-presentation key; querying it never exports mesh geometry.
struct QuaderUvPeelRevision final {
  SceneSessionId scene_session_id;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t interaction_revision = 0;

  friend bool operator==(const QuaderUvPeelRevision &,
                         const QuaderUvPeelRevision &) = default;
};

/// Presents the selected edge guides and retained peel settings together.
struct QuaderUvPeelSettings final {
  std::size_t selected_edge_count = 0;
  bool editable = false;
  QuaderUvPeelOptions options;
  QuaderUvPeelBlockReason block_reason = QuaderUvPeelBlockReason::NoSelection;
  std::string disabled_reason;
  std::uint64_t selection_token = 0;
};

/// Carries the project-owned material resolved from one polygon face.
struct QuaderFaceProjectMaterial {
  ProjectAssetId id;
};

/// Names one independently editable selected-face texture property.
enum class QuaderFaceTextureField {
  ScaleX,
  ScaleY,
  ShiftX,
  ShiftY,
  Rotate,
};

/// Names one selected-face texture action exposed by the editor and commands.
enum class QuaderFaceTextureAction {
  AlignToGrid,
  AlignToFace,
  AlignToView,
  ScaleXUp,
  ScaleXDown,
  ScaleYUp,
  ScaleYDown,
  ShiftLeft,
  ShiftRight,
  ShiftUp,
  ShiftDown,
  RotateClockwise,
  RotateCounterClockwise,
  FlipTextureHorizontal,
  FlipTextureVertical,
  FlipFaceHorizontal,
  FlipFaceVertical,
  FitBoth,
  FitX,
  FitY,
  JustifyLeft,
  JustifyRight,
  JustifyTop,
  JustifyBottom,
  JustifyCenter,
};

/// The common fallback name used by commands, accessibility, and history.
[[nodiscard]] std::string_view
quader_face_texture_action_label(QuaderFaceTextureAction action);

/// Pins a button or command action to the selection that was presented.
struct QuaderFaceTextureActionRequest final {
  QuaderFaceTextureAction action = QuaderFaceTextureAction::AlignToGrid;
  std::uint64_t selection_token = 0;
};

/// UI-localizable reason why selected face texture editing is unavailable.
enum class QuaderFaceTextureBlockReason {
  None,
  NoSelection,
  PreviewActive,
  TargetUnavailable,
};

/// Cheap retained-presentation key; querying it never exports mesh geometry.
struct QuaderFaceTextureRevision final {
  SceneSessionId scene_session_id;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t interaction_revision = 0;

  friend bool operator==(const QuaderFaceTextureRevision &,
                         const QuaderFaceTextureRevision &) = default;
};

/// Presents shared scales, pixel shifts, and selection-relative rotation.
struct QuaderFaceTextureSettings final {
  std::size_t selected_face_count = 0;
  bool editable = false;
  bool treat_as_one = false;
  QuaderFaceTextureBlockReason block_reason =
      QuaderFaceTextureBlockReason::NoSelection;
  std::string disabled_reason;
  std::optional<double> scale_x;
  std::optional<double> scale_y;
  double shift_x = 0.0;
  double shift_y = 0.0;
  double rotation_degrees = 0.0;
  std::uint64_t selection_token = 0;
};

/// A captured selection token rejects edits queued before selection changed.
/// Reuse a nonempty merge_key only for one continuous numeric editing gesture.
struct QuaderFaceTextureEdit final {
  QuaderFaceTextureField field = QuaderFaceTextureField::ScaleX;
  double value = 1.0;
  std::uint64_t selection_token = 0;
  std::string merge_key;
};

/**
 * Configures native public face thickening previews.
 */
struct QuaderThickenArgs {
  double thickness = 0.25;
  bool from_center = false;
};

/**
 * Configures native public quad slice previews.
 */
struct QuaderSliceQuadArgs {
  int x_slices = 1;
  int y_slices = 1;
};

/**
 * Configures native public merge-by-distance previews.
 */
struct QuaderMergeByDistanceArgs {
  double tolerance = 0.125;
};

/**
 * Describes the transient scene-local instance editing scope.
 */
struct QuaderInstanceEditState final {
  bool active = false;
  QuaderEntityRef occurrence;
  QuaderEntityRef definition;
  std::string scene_name;
  std::string instance_name;
};

/// Describes one scene-local hidden instance definition and its members.
struct QuaderInstanceDefinitionInfo final {
  SceneSessionId scene_session_id;
  QuaderEntityRef root;
  std::vector<QuaderEntityRef> members;
};

/// Describes one visible occurrence and its scene-local definition.
struct QuaderInstanceOccurrenceInfo final {
  SceneSessionId scene_session_id;
  QuaderEntityRef root;
  QuaderEntityRef definition;
};

/// Returns the definition and first occurrence produced by Create Instance.
struct QuaderInstanceCreateResult {
  QuaderInstanceDefinitionInfo definition;
  QuaderInstanceOccurrenceInfo occurrence;
};

/// Returns the ordinary roots produced by flattening selected occurrences.
struct QuaderInstanceFlattenResult {
  SceneSessionId scene_session_id;
  QuaderEntityRef definition;
  std::vector<QuaderEntityRef> root_entities;
};

/**
 * Handle to the currently active native modeling operation preview.
 */
class QuaderPreviewHandle {
public:
  QuaderPreviewHandle() = default;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] const QuaderApiVoidResult &begin_result() const;
  QuaderApiVoidResult update(const QuaderPreviewSettings &settings);
  QuaderApiVoidResult update(QuaderEdgeBevelArgs args);
  QuaderApiVoidResult update(QuaderThickenArgs args);
  QuaderApiVoidResult update(QuaderSliceQuadArgs args);
  QuaderApiVoidResult update(QuaderMergeByDistanceArgs args);
  QuaderApiVoidResult commit(std::string label = {});
  QuaderApiVoidResult cancel();

private:
  friend class QuaderModelingSelection;

  QuaderPreviewHandle(std::optional<QuaderApiSession> session,
                      QuaderApiMutationRunner &mutations,
                      std::string default_label,
                      QuaderApiVoidResult begin_status);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  SceneSessionId scene_session_id_;
  std::string default_label_;
  QuaderApiVoidResult begin_status_;
};

/**
 * Shared native public component-selection contract.
 */
class QuaderModelingSelection {
public:
  /**
   * Identifies which native modeling selection kind this wrapper edits.
   */
  enum class Kind {
    Object,
    Vertex,
    Edge,
    Face,
  };

  [[nodiscard]] QuaderModelingObjectRef object() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::size_t count() const;
  QuaderApiVoidResult replace() const;
  QuaderApiVoidResult add() const;
  QuaderApiVoidResult remove() const;
  QuaderApiVoidResult toggle() const;

protected:
  QuaderModelingSelection() = default;
  QuaderModelingSelection(std::optional<QuaderApiSession> session,
                          QuaderApiMutationRunner &mutations,
                          QuaderModelingObjectRef object, Kind kind);

  [[nodiscard]] bool valid_context() const;
  [[nodiscard]] QuaderApiVoidResult apply(QuaderSelectionEdit edit) const;
  [[nodiscard]] QuaderApiVoidResult execute_after_replace(
      std::string_view action_id) const;
  [[nodiscard]] QuaderPreviewHandle begin_preview_after_replace(
      std::string_view action_id, const QuaderPreviewSettings &settings,
      std::string default_label) const;

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
  Kind kind_ = Kind::Object;
  std::vector<QuaderElementId> elements_;
  std::vector<QuaderEdgeRef> edges_;
};

/**
 * Object selection wrapper for one mesh object.
 */
class QuaderObjectSelection final : public QuaderModelingSelection {
public:
  QuaderObjectSelection() = default;
  [[nodiscard]] QuaderMeshHandle mesh() const;
  [[nodiscard]] QuaderApiResult<QuaderMeshHandle> duplicate() const;
  QuaderApiVoidResult combine_selected() const;
  QuaderApiVoidResult snap_to_floor() const;

private:
  friend class QuaderMeshHandle;
  friend class QuaderMeshTransform;
  friend class QuaderMeshMaterials;
  QuaderObjectSelection(std::optional<QuaderApiSession> session,
                        QuaderApiMutationRunner &mutations,
                        QuaderModelingObjectRef object);
};

/**
 * Vertex selection wrapper for one mesh object.
 */
class QuaderVertexSelection final : public QuaderModelingSelection {
public:
  QuaderVertexSelection() = default;
  [[nodiscard]] std::span<const QuaderElementId> vertices() const;
  QuaderApiVoidResult snap_to_active() const;
  QuaderApiVoidResult snap_to_floor() const;
  QuaderApiVoidResult merge_to_active() const;
  QuaderApiVoidResult merge_to_center() const;
  QuaderPreviewHandle merge_by_distance(QuaderMergeByDistanceArgs args = {}) const;
  QuaderApiVoidResult remove_doubles(QuaderMergeByDistanceArgs args = {}) const;
  QuaderApiVoidResult bevel() const;
  QuaderApiVoidResult connect() const;
  QuaderApiVoidResult dissolve() const;
  QuaderApiVoidResult radial_align() const;

private:
  friend class QuaderMeshVertices;
  QuaderVertexSelection(std::optional<QuaderApiSession> session,
                        QuaderApiMutationRunner &mutations,
                        QuaderModelingObjectRef object,
                        std::vector<QuaderElementId> vertices);
};

/**
 * Edge selection wrapper for one mesh object.
 */
class QuaderEdgeSelection final : public QuaderModelingSelection {
public:
  QuaderEdgeSelection() = default;
  [[nodiscard]] std::span<const QuaderEdgeRef> edges() const;
  QuaderApiVoidResult snap_to_active() const;
  QuaderApiVoidResult connect() const;
  QuaderApiVoidResult split() const;
  QuaderApiVoidResult harden_normals() const;
  QuaderApiVoidResult soften_normals() const;
  QuaderPreviewHandle bevel(QuaderEdgeBevelArgs args = {}) const;
  QuaderApiVoidResult bridge() const;
  QuaderPreviewHandle interpolated_bridge(int steps = 1) const;
  QuaderApiVoidResult dissolve() const;
  QuaderApiVoidResult merge() const;
  QuaderApiVoidResult collapse() const;
  QuaderApiVoidResult fill_hole() const;
  QuaderApiVoidResult radial_align() const;

private:
  friend class QuaderMeshEdges;
  QuaderEdgeSelection(std::optional<QuaderApiSession> session,
                      QuaderApiMutationRunner &mutations,
                      QuaderModelingObjectRef object,
                      std::vector<QuaderEdgeRef> edges);
};

/**
 * Face selection wrapper for one mesh object.
 */
class QuaderFaceSelection final : public QuaderModelingSelection {
public:
  QuaderFaceSelection() = default;
  [[nodiscard]] std::span<const QuaderElementId> faces() const;
  QuaderApiVoidResult bridge() const;
  QuaderPreviewHandle interpolated_bridge(int steps = 1) const;
  QuaderApiVoidResult invert() const;
  QuaderApiVoidResult combine() const;
  QuaderApiVoidResult collapse() const;
  QuaderApiVoidResult radial_align() const;
  QuaderPreviewHandle slice(QuaderSliceQuadArgs args = {}) const;
  QuaderPreviewHandle thicken(QuaderThickenArgs args = {}) const;
  QuaderApiVoidResult extract() const;
  QuaderApiVoidResult detach() const;
  QuaderApiVoidResult shade_smooth() const;
  QuaderApiVoidResult shade_flat() const;
  QuaderApiVoidResult assign_material_slot(std::uint32_t material_slot) const;

private:
  friend class QuaderMeshFaces;
  QuaderFaceSelection(std::optional<QuaderApiSession> session,
                      QuaderApiMutationRunner &mutations,
                      QuaderModelingObjectRef object,
                      std::vector<QuaderElementId> faces);
};

/**
 * Vertex facet for one native mesh handle.
 */
class QuaderMeshVertices final {
public:
  QuaderMeshVertices() = default;
  [[nodiscard]] QuaderVertexSelection only(
      std::span<const QuaderElementId> vertices) const;
  [[nodiscard]] QuaderVertexSelection only(
      std::initializer_list<QuaderElementId> vertices) const;

private:
  friend class QuaderMeshHandle;
  QuaderMeshVertices(std::optional<QuaderApiSession> session,
                     QuaderApiMutationRunner &mutations,
                     QuaderModelingObjectRef object);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
};

/**
 * Edge facet for one native mesh handle.
 */
class QuaderMeshEdges final {
public:
  QuaderMeshEdges() = default;
  [[nodiscard]] QuaderEdgeSelection only(
      std::span<const QuaderEdgeRef> edges) const;
  [[nodiscard]] QuaderEdgeSelection only(
      std::initializer_list<QuaderEdgeRef> edges) const;

private:
  friend class QuaderMeshHandle;
  QuaderMeshEdges(std::optional<QuaderApiSession> session,
                  QuaderApiMutationRunner &mutations,
                  QuaderModelingObjectRef object);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
};

/**
 * Face facet for one native mesh handle.
 */
class QuaderMeshFaces final {
public:
  QuaderMeshFaces() = default;
  [[nodiscard]] QuaderFaceSelection only(
      std::span<const QuaderElementId> faces) const;
  [[nodiscard]] QuaderFaceSelection only(
      std::initializer_list<QuaderElementId> faces) const;

private:
  friend class QuaderMeshHandle;
  QuaderMeshFaces(std::optional<QuaderApiSession> session,
                  QuaderApiMutationRunner &mutations,
                  QuaderModelingObjectRef object);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
};

/**
 * Transform facet for one native mesh handle.
 */
class QuaderMeshTransform final {
public:
  QuaderMeshTransform() = default;
  QuaderApiVoidResult translate(QVec3 delta) const;
  QuaderApiVoidResult rotate(QVec3 euler_radians, QVec3 pivot = {}) const;
  QuaderApiVoidResult scale(QVec3 scale, QVec3 pivot = {}) const;
  QuaderApiVoidResult apply(const QuaderTransform3 &transform,
                            QVec3 pivot = {}) const;

private:
  friend class QuaderMeshHandle;
  QuaderMeshTransform(std::optional<QuaderApiSession> session,
                      QuaderApiMutationRunner &mutations,
                      QuaderModelingObjectRef object);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
};

/**
 * Material facet for one native mesh handle.
 */
class QuaderMeshMaterials final {
public:
  QuaderMeshMaterials() = default;
  QuaderApiVoidResult assign_slot(std::uint32_t material_slot) const;
  QuaderApiVoidResult assign_to_face(QuaderElementId face,
                                     std::uint32_t material_slot) const;
  [[nodiscard]] QuaderApiResult<QuaderFaceProjectMaterial>
  project_material_for_face(QuaderElementId face) const;
  QuaderApiVoidResult
  apply_face_material_to_mesh(QuaderElementId face) const;

private:
  friend class QuaderMeshHandle;
  QuaderMeshMaterials(std::optional<QuaderApiSession> session,
                      QuaderApiMutationRunner &mutations,
                      QuaderModelingObjectRef object);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef object_;
};

/**
 * Value handle for native public mesh-object workflows.
 */
class QuaderMeshHandle {
public:
  QuaderMeshHandle() = default;

  [[nodiscard]] QuaderModelingObjectRef ref() const;
  [[nodiscard]] QuaderModelingObjectId id() const;
  [[nodiscard]] bool valid() const;
  [[nodiscard]] QuaderObjectSelection select() const;
  [[nodiscard]] QuaderMeshVertices vertices() const;
  [[nodiscard]] QuaderMeshEdges edges() const;
  [[nodiscard]] QuaderMeshFaces faces() const;
  [[nodiscard]] QuaderMeshTransform transform() const;
  [[nodiscard]] QuaderMeshMaterials materials() const;
  [[nodiscard]] std::optional<QuaderMeshAsset> compiled_mesh() const;

private:
  friend class QuaderModelingApi;
  friend class QuaderObjectSelection;
  QuaderMeshHandle(std::optional<QuaderApiSession> session,
                   QuaderApiMutationRunner &mutations,
                   QuaderModelingObjectRef ref);

  std::optional<QuaderApiSession> session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
  QuaderModelingObjectRef ref_;
};

/**
 * Exposes Quader Modeling API operations through the public Quader API surface.
 */
class QuaderModelingApi final {
public:
  QuaderModelingApi(QuaderApiSession session,
                    QuaderApiMutationRunner &mutations);

  QuaderApiResult<QuaderModelingObjectRef>
  create_box(const QuaderCreateBoxArgs &args);
  QuaderApiResult<QuaderModelingObjectRef> create_box(QuaderBounds bounds,
                                                      std::string name = "Box");
  QuaderApiResult<QuaderMeshHandle>
  create_box_mesh(const QuaderCreateBoxArgs &args);
  QuaderApiResult<QuaderMeshHandle>
  create_box_mesh(QuaderBounds bounds, std::string name = "Box");
  QuaderApiResult<QuaderModelingObjectRef>
  commit_box_preview(std::string name = "Box",
                     AssetId default_material = kInvalidAssetId);

  QuaderApiResult<QuaderModelingObjectRef>
  create_mesh(QuaderPolygonDocument document, QuaderCreateMeshArgs args = {});
  QuaderApiResult<QuaderMeshHandle>
  create_mesh_handle(QuaderPolygonDocument document,
                     QuaderCreateMeshArgs args = {});
  QuaderApiResult<QuaderLinearArrayResult>
  create_linear_array(const QuaderLinearArrayArgs &args);
  QuaderApiResult<QuaderRadialArrayResult>
  create_radial_array(const QuaderRadialArrayArgs &args);
  [[nodiscard]] QuaderMeshHandle mesh(QuaderModelingObjectRef object) const;
  [[nodiscard]] QuaderApiResult<QuaderMeshHandle>
  mesh(QuaderModelingObjectId object_id) const;

  QuaderApiVoidResult set_selection_mode(QuaderSelectionMode mode);
  [[nodiscard]] QuaderFaceTextureRevision face_texture_settings_revision() const;
  [[nodiscard]] QuaderFaceTextureSettings face_texture_settings() const;
  QuaderApiVoidResult
  set_face_texture_setting(const QuaderFaceTextureEdit &edit);
  QuaderApiVoidResult
  apply_face_texture_action(const QuaderFaceTextureActionRequest &request);
  /// Continues the sole selected face's material/UV chart onto a target face by
  /// rotating the chart about the planes' intersection, retaining the selection.
  QuaderApiVoidResult transfer_face_texture_to_face(
      QuaderModelingObjectId object_id, QuaderElementId face_id);
  [[nodiscard]] bool face_texture_treat_as_one() const;
  QuaderApiVoidResult set_face_texture_treat_as_one(bool enabled);
  [[nodiscard]] QuaderUvPeelRevision uv_peel_settings_revision() const;
  [[nodiscard]] QuaderUvPeelSettings uv_peel_settings() const;
  QuaderApiVoidResult set_uv_peel_options(const QuaderUvPeelOptions &options);
  QuaderApiVoidResult peel_uv(const QuaderUvPeelRequest &request);
  [[nodiscard]] bool can_execute_tool(std::string_view tool_id) const;
  QuaderApiVoidResult execute_tool(std::string_view tool_id);
  QuaderApiVoidResult delete_selection();
  [[nodiscard]] bool can_cut_selection() const;
  QuaderApiVoidResult cut_selection();
  QuaderApiVoidResult copy_selection();
  QuaderApiVoidResult paste_selection();
  QuaderApiVoidResult repeat_last_action();
  QuaderApiVoidResult duplicate_meshes();
  QuaderApiVoidResult combine_meshes();
  QuaderApiVoidResult remove_doubles();
  [[nodiscard]] bool can_copy_pivot_from_active() const;
  QuaderApiVoidResult copy_pivot_from_active();
  [[nodiscard]] bool can_snap_selected_meshes_to_floor() const;
  QuaderApiVoidResult snap_selected_meshes_to_floor();
  [[nodiscard]] bool can_snap_selected_vertices_to_floor() const;
  QuaderApiVoidResult snap_selected_vertices_to_floor();
  [[nodiscard]] bool can_align_selected_meshes() const;
  [[nodiscard]] bool can_distribute_selected_meshes() const;
  QuaderApiVoidResult align_selected_meshes(
      QuaderTransformAxis axis, QuaderMeshAlignment alignment,
      QuaderMeshArrangementOptions options = {});
  QuaderApiVoidResult distribute_selected_meshes(
      QuaderTransformAxis axis, QuaderMeshDistribution distribution,
      QuaderMeshArrangementOptions options = {});

  QuaderApiVoidResult begin_preview(std::string_view action_id);
  QuaderApiVoidResult update_preview(const QuaderPreviewSettings &settings);
  QuaderApiVoidResult commit_preview(std::string label = {});
  QuaderApiVoidResult cancel_preview();
  [[nodiscard]] bool can_cancel_active_tool_or_preview() const;

  QuaderApiVoidResult begin_transform_preview(bool duplicate_selected = false);
  QuaderApiVoidResult update_transform_translate(QVec3 delta);
  QuaderApiVoidResult update_transform_matrix(const QuaderTransform3 &transform,
                                              QVec3 pivot);
  QuaderApiVoidResult
  update_transform_extrude(QVec3 offset, float closed_edge_ledge_size = 0.0f);
  QuaderApiVoidResult update_transform_inset(const QuaderTransform3 &transform,
                                             QVec3 pivot);
  QuaderApiVoidResult commit_transform_preview(std::string label = {});
  QuaderApiVoidResult cancel_transform_preview();

  QuaderApiResult<QuaderTransformDragPreview>
  begin_transform_drag(const QuaderTransformDragBeginRequest &request);
  QuaderApiResult<QuaderTransformDragPreview>
  update_transform_drag(const QuaderTransformDragUpdateRequest &request);
  QuaderApiResult<QuaderTransformDragPreview> commit_transform_drag();
  QuaderApiVoidResult cancel_transform_drag();

  QuaderApiVoidResult move_selection(QVec3 delta);
  QuaderApiVoidResult rotate_selection(QVec3 euler_radians, QVec3 pivot = {});
  QuaderApiVoidResult scale_selection(QVec3 scale, QVec3 pivot = {});
  QuaderApiVoidResult transform_selection(const QuaderTransform3 &transform,
                                          QVec3 pivot = {});
  [[nodiscard]] bool can_duplicate_selected_entity_hierarchy() const;
  QuaderApiVoidResult duplicate_selected_entity_hierarchy();
  QuaderApiVoidResult duplicate_selected_hierarchy_for_group_drag();
  [[nodiscard]] bool can_create_instance() const;
  QuaderApiVoidResult create_instance();
  /** Public intent names used by UI, scripts, and automation. */
  [[nodiscard]] bool can_create_instance_from_selection() const;
  QuaderApiResult<QuaderInstanceCreateResult>
  create_instance_from_selection();
  [[nodiscard]] bool can_flatten_instance() const;
  QuaderApiVoidResult flatten_instance();
  [[nodiscard]] bool can_flatten_selected_instances() const;
  QuaderApiResult<QuaderInstanceFlattenResult>
  flatten_selected_instances();
  [[nodiscard]] bool can_enter_instance_edit(EntityId occurrence) const;
  QuaderApiVoidResult enter_instance_edit(EntityId occurrence);
  [[nodiscard]] bool can_exit_instance_edit() const;
  QuaderApiVoidResult exit_instance_edit();
  /** Cheap change token for retained UI that presents instance edit state. */
  [[nodiscard]] std::uint64_t instance_edit_revision() const;
  [[nodiscard]] QuaderInstanceEditState instance_edit_state() const;

  QuaderApiVoidResult apply_active_material(std::uint32_t material_slot = 0);
  QuaderApiVoidResult apply_active_material_to_mesh(
      QuaderModelingObjectId object_id, QuaderElementId face_id);
  QuaderApiVoidResult apply_active_material_to_face(
      QuaderModelingObjectId object_id, QuaderElementId face_id);
  [[nodiscard]] QuaderApiResult<QuaderFaceProjectMaterial>
  project_material_for_face(QuaderModelingObjectId object_id,
                            QuaderElementId face_id) const;
  QuaderApiVoidResult
  apply_face_material_to_mesh(QuaderModelingObjectId object_id,
                              QuaderElementId face_id);
  QuaderApiVoidResult
  assign_material_slot_to_selection(std::uint32_t material_slot);
  QuaderApiVoidResult
  assign_material_slot_to_face(QuaderModelingObjectRef object,
                               QuaderElementId face,
                               std::uint32_t material_slot);
  QuaderApiVoidResult cut_selected_meshes_by_hovered_face_plane();

  [[nodiscard]] QuaderActionSet
  available_action_set(const QuaderActionQuery &query = {}) const;
  [[nodiscard]] std::vector<QuaderActionItem>
  available_actions(const QuaderActionQuery &query = {}) const;
  [[nodiscard]] std::vector<QuaderInstantOperationDescriptor>
  instant_operations() const;
  [[nodiscard]] QuaderSelectionSummary selection_summary() const;
  [[nodiscard]] std::vector<QuaderModelingObjectRecord> objects() const;

private:
  /// Caches mesh-arrangement availability against every owner revision that
  /// can change the answer without rebuilding command predicates per frame.
  struct MeshArrangementAvailabilityCache final {
    const void *scene_identity = nullptr;
    std::uint64_t scene_session_id = 0;
    std::uint64_t scene_content_revision = 0;
    std::uint64_t scene_selection_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t modeling_content_revision = 0;
    std::uint64_t modeling_selection_revision = 0;
    std::uint64_t modeling_object_map_revision = 0;
    bool can_align = false;
    bool can_distribute = false;
    bool valid = false;
  };

  [[nodiscard]] const MeshArrangementAvailabilityCache &
  mesh_arrangement_availability() const;
  [[nodiscard]] QuaderModelingObjectRef
  ref_from_record(const QuaderModelingObjectRecord &record) const;

  QuaderApiSession session_;
  QuaderApiMutationRunner &mutations_;
  mutable MeshArrangementAvailabilityCache mesh_arrangement_availability_;
};

} // namespace quader
