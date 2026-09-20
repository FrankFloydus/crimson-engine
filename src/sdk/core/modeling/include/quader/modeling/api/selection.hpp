////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_contracts.hpp>

namespace quader::modeling {

/**
 * Shared immutable selection contract for typed selections.
 */
class Selection {
public:
  Selection() = default;
  Selection(std::shared_ptr<ModelingApiContext> context,
            SelectionUnion selection);
  [[nodiscard]] SelectionKind kind() const;
  [[nodiscard]] ObjectId object() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] bool is_object_selection() const;
  [[nodiscard]] bool is_vertex_selection() const;
  [[nodiscard]] bool is_edge_selection() const;
  [[nodiscard]] bool is_face_selection() const;
  [[nodiscard]] OperationReceipt
  apply(SelectionEdit edit = SelectionEdit::Replace) const;
  [[nodiscard]] OperationReceipt replace() const;
  [[nodiscard]] OperationReceipt add() const;
  [[nodiscard]] OperationReceipt remove() const;
  [[nodiscard]] OperationReceipt toggle() const;
  [[nodiscard]] SelectionUnion to_union() const;

protected:
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
  SelectionUnion selection_;
};

/**
 * Object selection value with object-level operations.
 */
class ObjectSelection final : public Selection {
public:
  ObjectSelection();
  ObjectSelection(std::shared_ptr<ModelingApiContext> context,
                  SelectionUnion selection);
  [[nodiscard]] std::span<const ObjectId> objects() const;
  [[nodiscard]] OperationReceipt delete_objects() const;
  [[nodiscard]] MeshHandle duplicate(DuplicateOptions options = {}) const;
  [[nodiscard]] OperationReceipt combine() const;

private:
  friend class MeshCollection;
  friend class MeshHandle;
  friend class SelectionApi;
};

/**
 * Vertex selection value with vertex operations.
 */
class VertexSelection final : public Selection {
public:
  VertexSelection();
  VertexSelection(std::shared_ptr<ModelingApiContext> context,
                  SelectionUnion selection);
  [[nodiscard]] std::span<const VertexId> vertices() const;
  [[nodiscard]] OperationReceipt snap_to_active() const;
  [[nodiscard]] OperationReceipt merge_to_active() const;
  [[nodiscard]] OperationReceipt merge_to_center() const;
  [[nodiscard]] PreviewHandle
  merge_by_distance(MergeByDistanceOptions options = {}) const;
  [[nodiscard]] OperationReceipt
  remove_doubles(MergeByDistanceOptions options = {}) const;
  [[nodiscard]] OperationReceipt bevel(BevelVerticesOptions options = {}) const;
  [[nodiscard]] OperationReceipt connect() const;
  [[nodiscard]] OperationReceipt dissolve() const;
  [[nodiscard]] OperationReceipt radial_align() const;

private:
  friend class MeshVertices;
  friend class SelectionApi;
};

/**
 * Edge selection value with edge operations.
 */
class EdgeSelection final : public Selection {
public:
  EdgeSelection();
  EdgeSelection(std::shared_ptr<ModelingApiContext> context,
                SelectionUnion selection);
  [[nodiscard]] std::span<const EdgeKey> edges() const;
  [[nodiscard]] OperationReceipt snap_to_active() const;
  [[nodiscard]] OperationReceipt connect() const;
  [[nodiscard]] OperationReceipt split() const;
  [[nodiscard]] OperationReceipt reset_normals_to_default() const;
  [[nodiscard]] OperationReceipt harden_normals() const;
  [[nodiscard]] OperationReceipt soften_normals() const;
  [[nodiscard]] PreviewHandle bevel(EdgeBevelOptions options = {}) const;
  [[nodiscard]] OperationReceipt bridge(BridgeOptions options = {}) const;
  [[nodiscard]] PreviewHandle
  interpolated_bridge(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt bridge_pairs(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt
  bridge_boundaries(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt dissolve() const;
  [[nodiscard]] OperationReceipt merge() const;
  [[nodiscard]] OperationReceipt collapse() const;
  [[nodiscard]] OperationReceipt fill_hole() const;
  [[nodiscard]] OperationReceipt radial_align() const;
  [[nodiscard]] OperationReceipt insert_loop(InsertEdgeLoopOptions options) const;

private:
  friend class MeshEdges;
  friend class SelectionApi;
};

/**
 * Face selection value with face operations.
 */
class FaceSelection final : public Selection {
public:
  FaceSelection();
  FaceSelection(std::shared_ptr<ModelingApiContext> context,
                SelectionUnion selection);
  [[nodiscard]] std::span<const FaceId> faces() const;
  [[nodiscard]] OperationReceipt bridge(BridgeOptions options = {}) const;
  [[nodiscard]] PreviewHandle
  interpolated_bridge(BridgeOptions options = {}) const;
  [[nodiscard]] OperationReceipt flip_normals() const;
  [[nodiscard]] OperationReceipt
  recalculate_normals(RecalculateNormalsOptions options = {}) const;
  [[nodiscard]] OperationReceipt shade_smooth() const;
  [[nodiscard]] OperationReceipt shade_flat() const;
  [[nodiscard]] OperationReceipt combine() const;
  [[nodiscard]] OperationReceipt collapse() const;
  [[nodiscard]] OperationReceipt radial_align() const;
  [[nodiscard]] MeshHandle extract() const;
  [[nodiscard]] OperationReceipt detach() const;
  [[nodiscard]] PreviewHandle slice_quads(SliceQuadOptions options = {}) const;
  [[nodiscard]] PreviewHandle thicken(ThickenOptions options = {}) const;
  [[nodiscard]] OperationReceipt extrude(ExtrudeOptions options = {}) const;
  [[nodiscard]] OperationReceipt extrude(FaceExtrudeOptions options) const;
  [[nodiscard]] OperationReceipt inset(InsetOptions options = {}) const;
  [[nodiscard]] OperationReceipt delete_faces() const;

private:
  friend class MeshFaces;
  friend class SelectionApi;
};

/**
 * Root selection facade for session selection and host-projected targets.
 */
class SelectionApi {
public:
  SelectionApi() = default;
  [[nodiscard]] SelectionEdit default_edit() const;
  [[nodiscard]] OperationReceipt set_default_edit(SelectionEdit edit);
  [[nodiscard]] OperationReceipt clear();
  [[nodiscard]] OperationReceipt select_all();
  [[nodiscard]] OperationReceipt invert();
  [[nodiscard]] ObjectSelection
  objects(std::span<const ObjectId> objects,
          SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] VertexSelection
  vertices(ObjectId object, std::span<const VertexId> vertices,
           SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] EdgeSelection edges(ObjectId object,
                                    std::span<const EdgeKey> edges,
                                    SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] FaceSelection faces(ObjectId object,
                                    std::span<const FaceId> faces,
                                    SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] PickResult pick(PickRequest request);
  [[nodiscard]] OperationReceipt pick_select(PickSelectRequest request);
  [[nodiscard]] BoxSelectionBaseline capture_box_baseline();
  [[nodiscard]] OperationReceipt preview_box(BoxSelectionRequest request);
  [[nodiscard]] OperationReceipt commit_box(BoxSelectionRequest request);
  [[nodiscard]] HoverResult preview_hover(HoverRequest request);
  [[nodiscard]] OperationReceipt apply_hover(HoverTarget target);
  [[nodiscard]] OperationReceipt clear_hover();
  [[nodiscard]] SelectionSummary summary() const;

private:
  friend class ModelingApi;
  explicit SelectionApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

} // namespace quader::modeling
