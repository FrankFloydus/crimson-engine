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
  [[nodiscard]] bool can_execute_tool(std::string_view tool_id) const;
  QuaderApiVoidResult execute_tool(std::string_view tool_id);
  QuaderApiVoidResult delete_selection();
  QuaderApiVoidResult copy_selection();
  QuaderApiVoidResult paste_selection();
  QuaderApiVoidResult repeat_last_action();
  QuaderApiVoidResult duplicate_meshes();
  QuaderApiVoidResult combine_meshes();
  QuaderApiVoidResult remove_doubles();
  [[nodiscard]] bool can_copy_pivot_from_active() const;
  QuaderApiVoidResult copy_pivot_from_active();
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

  QuaderApiVoidResult apply_active_material(std::uint32_t material_slot = 0);
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
  [[nodiscard]] QuaderModelingObjectRef
  ref_from_record(const QuaderModelingObjectRecord &record) const;

  QuaderApiSession session_;
  QuaderApiMutationRunner &mutations_;
};

} // namespace quader
