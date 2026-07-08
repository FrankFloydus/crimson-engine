////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/ids.hpp>
#include <quader/modeling/operations/operation_result.hpp>
#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>
#include <quader/modeling/types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace quader::modeling {

struct PolygonDocumentImpl;
class PolygonDocumentNativeAccess;

/**
 * Enumerates SDK polygon component selection modes.
 */
enum class SelectionMode {
  Vertex,
  Edge,
  Face,
};

/**
 * Defines the bounds and material used to create a box document.
 */
struct BoxSpec {
  Vec3 min{-1.0F, -1.0F, -1.0F};
  Vec3 max{1.0F, 1.0F, 1.0F};
  MaterialId material{};
};

/**
 * Captures polygon component selection state with SDK typed IDs.
 */
struct PolygonSelectionSnapshot {
  SelectionKind kind = SelectionKind::Vertex;
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
  bool has_active = false;
  VertexId active_vertex{};
  EdgeKey active_edge{};
  FaceId active_face{};
};

/**
 * SDK edge bevel options that mirror the current polygon operation defaults.
 */
struct EdgeBevelSpec {
  float width = 1.0F;
  float profile = 0.5F;
  int segments = 1;
};

struct ExtractedPolygonDocument;

/**
 * Owns an SDK polygon document facade over portable polygon operations.
 */
class PolygonDocument {
public:
  PolygonDocument();
  PolygonDocument(const PolygonDocument &other);
  PolygonDocument(PolygonDocument &&other) noexcept;
  PolygonDocument &operator=(const PolygonDocument &other);
  PolygonDocument &operator=(PolygonDocument &&other) noexcept;
  ~PolygonDocument();

  [[nodiscard]] static Result<PolygonDocument> make_box(const BoxSpec &spec);
  [[nodiscard]] static Result<PolygonDocument>
  make_face(std::span<const Vec3> points, MaterialId material = {});

  [[nodiscard]] std::size_t vertex_count() const;
  [[nodiscard]] std::size_t face_count() const;
  [[nodiscard]] std::uint64_t content_revision() const;
  [[nodiscard]] std::uint64_t selection_revision() const;
  [[nodiscard]] std::vector<VertexId> vertex_ids() const;
  [[nodiscard]] std::vector<EdgeKey> edge_ids() const;
  [[nodiscard]] std::vector<FaceId> face_ids() const;
  [[nodiscard]] std::vector<EdgeKey> hard_edge_ids() const;
  [[nodiscard]] std::vector<EdgeKey> soft_edge_ids() const;
  [[nodiscard]] std::vector<FaceId>
  face_ids_by_material_slot(std::uint32_t material_slot) const;
  [[nodiscard]] std::vector<FaceId>
  face_ids_by_normal(Vec3 direction, float tolerance = 0.01F) const;
  [[nodiscard]] PolygonSelectionSnapshot selection() const;
  [[nodiscard]] Result<AuthoredPolygonPayload> authored_payload() const;
  [[nodiscard]] Result<MeshPayload> compile_mesh() const;
  [[nodiscard]] Result<OperationResult> validate() const;

  [[nodiscard]] Result<OperationResult> set_selection_mode(SelectionMode mode);
  [[nodiscard]] Result<OperationResult> clear_selection();
  [[nodiscard]] Result<OperationResult>
  select_vertices(std::span<const VertexId> vertex_ids,
                  SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult>
  select_edges(std::span<const EdgeKey> edges,
               SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult>
  select_faces(std::span<const FaceId> face_ids,
               SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult> select_vertex(VertexId vertex_id);
  [[nodiscard]] Result<OperationResult> select_edge(EdgeKey edge);
  [[nodiscard]] Result<OperationResult> select_face(FaceId face_id);
  [[nodiscard]] Result<OperationResult> select_all_vertices();
  [[nodiscard]] Result<OperationResult> select_all_edges();
  [[nodiscard]] Result<OperationResult> select_all_faces();
  [[nodiscard]] Result<OperationResult>
  apply_selection(const PolygonSelectionSnapshot &selection,
                  SelectionEdit edit = SelectionEdit::Replace);
  [[nodiscard]] Result<OperationResult> translate_selection(Vec3 delta);
  [[nodiscard]] Result<OperationResult>
  transform_selection(Transform3 transform);
  [[nodiscard]] Result<OperationResult>
  assign_selected_face_material_slot(std::uint32_t material_slot);
  [[nodiscard]] Result<OperationResult>
  assign_face_material_slot(FaceId face_id, std::uint32_t material_slot);
  [[nodiscard]] Result<OperationResult> snap_selected_vertices_to_active();
  [[nodiscard]] Result<OperationResult> merge_selected_vertices_to_active();
  [[nodiscard]] Result<OperationResult> merge_selected_vertices_to_center();
  [[nodiscard]] Result<OperationResult>
  merge_selected_vertices_by_distance(float tolerance);
  [[nodiscard]] Result<OperationResult> remove_double_vertices();
  [[nodiscard]] Result<OperationResult> bevel_selected_vertices(float distance);
  [[nodiscard]] Result<OperationResult> connect_selected_vertices();
  [[nodiscard]] Result<OperationResult> dissolve_selected_vertices();
  [[nodiscard]] Result<OperationResult> connect_selected_edges();
  [[nodiscard]] Result<OperationResult> snap_selected_edges_to_active();
  [[nodiscard]] Result<OperationResult> split_selected_edges();
  [[nodiscard]] Result<OperationResult> dissolve_selected_edges();
  [[nodiscard]] Result<OperationResult> merge_selected_edges();
  [[nodiscard]] Result<OperationResult> collapse_selected_edges();
  [[nodiscard]] Result<OperationResult> fill_hole_from_selected_edges();
  [[nodiscard]] Result<OperationResult>
  bevel_selected_edges(EdgeBevelSpec settings = {});
  [[nodiscard]] Result<OperationResult>
  bridge_selected_edges(int steps = 1);
  [[nodiscard]] Result<OperationResult> combine_selected_faces();
  [[nodiscard]] Result<OperationResult> collapse_selected_faces();
  [[nodiscard]] Result<OperationResult> radial_align_selection();
  [[nodiscard]] Result<OperationResult> flip_selected_face_normals();
  [[nodiscard]] Result<OperationResult>
  recalculate_selected_face_normals(bool outside = true);
  [[nodiscard]] Result<OperationResult> shade_selected_faces_smooth();
  [[nodiscard]] Result<OperationResult> shade_selected_faces_flat();
  [[nodiscard]] Result<OperationResult> harden_selected_edge_normals();
  [[nodiscard]] Result<OperationResult> soften_selected_edge_normals();
  [[nodiscard]] Result<OperationResult> delete_selection();
  [[nodiscard]] Result<OperationResult>
  insert_edge_loop(float factor = 0.5F);
  [[nodiscard]] Result<OperationResult>
  knife_segment(KnifeTarget previous, KnifeTarget current);
  [[nodiscard]] Result<OperationResult>
  knife_stroke(std::span<const KnifeTarget> points,
               std::span<const KnifeStrokeSegment> segments);
  [[nodiscard]] Result<OperationResult>
  slice_selected_quads(int x_slices = 1, int y_slices = 1);
  [[nodiscard]] Result<OperationResult>
  extrude_selected_elements(Vec3 offset,
                            float closed_edge_ledge_size = 0.0F);
  [[nodiscard]] Result<OperationResult>
  inset_selected_elements(Transform3 transform, Vec3 pivot);
  [[nodiscard]] Result<OperationResult>
  inset_selected_faces(float amount = 0.18F);
  [[nodiscard]] Result<OperationResult>
  extrude_selected_faces(float distance = 0.45F);
  [[nodiscard]] Result<OperationResult> detach_selected_faces();
  [[nodiscard]] Result<ExtractedPolygonDocument> extract_selected_faces();
  [[nodiscard]] Result<OperationResult>
  bridge_selected_faces(int steps = 1);
  [[nodiscard]] Result<OperationResult>
  thicken_selected_faces(float thickness = 0.25F,
                         bool from_center = false);

private:
  friend class PolygonDocumentNativeAccess;

  explicit PolygonDocument(std::unique_ptr<PolygonDocumentImpl> impl);

  std::unique_ptr<PolygonDocumentImpl> impl_;
};

/**
 * Result for extracting faces into a new SDK polygon document.
 */
struct ExtractedPolygonDocument {
  OperationResult receipt;
  PolygonDocument document;
};

} // namespace quader::modeling
