////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_topology.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_poly {

/**
 * Groups polygon element IDs by kind for operation delta reporting.
 */
struct OperationElementDelta {
  std::vector<ElementId> vertices;
  std::vector<Edge> edges;
  std::vector<ElementId> faces;

  [[nodiscard]] bool empty() const {
    return vertices.empty() && edges.empty() && faces.empty();
  }
};

/**
 * Stores the Operation Result data contract used by the polygon document and
 * mesh editing core.
 */
struct OperationResult {
  bool changed = false;
  std::string message;
  OperationElementDelta created;
  OperationElementDelta deleted;
  OperationElementDelta affected;
};

/**
 * Enumerates PlaneCutMode values used by the modeling layer.
 */
enum class PlaneCutMode {
  KeepBack = 0,
  KeepFront = 1,
  KeepBoth = 2,
};

/**
 * Stores the Plane Cut Request data contract used by the polygon document and
 * mesh editing core.
 */
struct PlaneCutRequest {
  quader::QVec3 first_point;
  quader::QVec3 second_point;
  quader::QVec3 third_point;
  PlaneCutMode mode = PlaneCutMode::KeepBoth;
};

/**
 * Stores the Plane Cut Result data contract used by the polygon document and
 * mesh editing core.
 */
struct PlaneCutResult {
  bool changed = false;
  Document front_document;
  Selection front_selection;
  Document back_document;
  Selection back_selection;
  int split_count = 0;
  int removed_count = 0;
  std::string message;
  OperationElementDelta created;
  OperationElementDelta deleted;
  OperationElementDelta affected;
};

/**
 * Enumerates BevelProfileType values used by the modeling layer.
 */
enum class BevelProfileType {
  Offset = 0,
};

/**
 * Represents an Edge Bevel Settings value used by the polygon document and mesh
 * editing core.
 */
struct EdgeBevelSettings {
  float width = 1.0F;
  // Profile in the 0..1 range; 0.5 is circular and 1 is the exact square
  // endpoint.
  float profile = 0.5F;
  BevelProfileType profile_type = BevelProfileType::Offset;
  int segments = 1;
};

/**
 * Enumerates KnifePointTargetKind values used by the modeling layer.
 */
enum class KnifePointTargetKind {
  ExistingVertex,
  ExistingEdge,
  InsertedVertex,
  FacePoint,
};

/**
 * Represents a Knife Point Target value used by the polygon document and mesh
 * editing core.
 */
struct KnifePointTarget {
  KnifePointTargetKind kind = KnifePointTargetKind::ExistingVertex;
  ElementId vertex_id = kInvalidElementId;
  Edge edge;
  float edge_factor = 0.5F;
  ElementId face_id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents a Knife Stroke Segment value used by the polygon document and mesh
 * editing core.
 */
struct KnifeStrokeSegment {
  std::uint32_t first_point = 0;
  std::uint32_t second_point = 0;
};

/**
 * Stores the Extract Faces Result data contract used by the polygon document
 * and mesh editing core.
 */
struct ExtractFacesResult {
  bool changed = false;
  Document extracted_document;
  Selection extracted_selection;
  std::string message;
  OperationElementDelta created;
  OperationElementDelta deleted;
  OperationElementDelta affected;
};

void ensure_face_uvs(Document &document);
void clear_face_uvs(Document &document);
[[nodiscard]] OperationResult
assign_selected_face_material_slot(Document &document,
                                   const Selection &selection,
                                   std::uint32_t material_slot);
[[nodiscard]] OperationResult
assign_face_material_slot(Document &document, ElementId face_id,
                          std::uint32_t material_slot);
[[nodiscard]] OperationResult translate_selection(Document &document,
                                                  const Selection &selection,
                                                  quader::QVec3 delta);
[[nodiscard]] OperationResult transform_selection(Document &document,
                                                  const Selection &selection,
                                                  const Transform3 &transform);
[[nodiscard]] OperationResult
snap_selected_vertices_to_active(Document &document, Selection &selection);
[[nodiscard]] OperationResult
merge_selected_vertices_to_active(Document &document, Selection &selection);
[[nodiscard]] OperationResult
merge_selected_vertices_to_center(Document &document, Selection &selection);
[[nodiscard]] OperationResult
merge_selected_vertices_by_distance(Document &document, Selection &selection,
                                    float tolerance);
[[nodiscard]] OperationResult remove_double_vertices(Document &document,
                                                     Selection &selection);
[[nodiscard]] OperationResult bevel_selected_vertices(Document &document,
                                                      Selection &selection,
                                                      float distance);
[[nodiscard]] OperationResult connect_selected_vertices(Document &document,
                                                        Selection &selection);
[[nodiscard]] OperationResult dissolve_selected_vertices(Document &document,
                                                         Selection &selection);
[[nodiscard]] OperationResult connect_selected_edges(Document &document,
                                                     Selection &selection);
[[nodiscard]] OperationResult split_selected_edges(Document &document,
                                                   Selection &selection);
[[nodiscard]] OperationResult dissolve_selected_edges(Document &document,
                                                      Selection &selection);
[[nodiscard]] OperationResult merge_selected_edges(Document &document,
                                                   Selection &selection);
[[nodiscard]] OperationResult collapse_selected_edges(Document &document,
                                                      Selection &selection);
[[nodiscard]] OperationResult
fill_hole_from_selected_edges(Document &document, Selection &selection);
[[nodiscard]] OperationResult
bevel_selected_edges(Document &document, Selection &selection,
                     const EdgeBevelSettings &settings = {});
[[nodiscard]] OperationResult
bridge_edge_pairs(Document &document, Selection &selection,
                  const std::vector<std::pair<Edge, Edge>> &edge_pairs,
                  int steps = 1);
[[nodiscard]] OperationResult
bridge_edge_boundaries(Document &document, Selection &selection,
                       std::span<const ElementId> first_vertices,
                       std::span<const ElementId> second_vertices, bool closed,
                       int steps = 1);
[[nodiscard]] OperationResult
bridge_selected_edges(Document &document, Selection &selection, int steps = 1);
[[nodiscard]] OperationResult combine_selected_faces(Document &document,
                                                     Selection &selection);
[[nodiscard]] OperationResult collapse_selected_faces(Document &document,
                                                      Selection &selection);
[[nodiscard]] OperationResult radial_align_selection(Document &document,
                                                     Selection &selection);
[[nodiscard]] OperationResult
flip_selected_face_normals(Document &document, const Selection &selection);
[[nodiscard]] OperationResult
recalculate_selected_face_normals(Document &document,
                                  const Selection &selection, bool outside);
[[nodiscard]] OperationResult
shade_selected_faces_smooth(Document &document, const Selection &selection);
[[nodiscard]] OperationResult
shade_selected_faces_flat(Document &document, const Selection &selection);
[[nodiscard]] OperationResult
harden_selected_edge_normals(Document &document, const Selection &selection);
[[nodiscard]] OperationResult
soften_selected_edge_normals(Document &document, const Selection &selection);
[[nodiscard]] OperationResult delete_selection(Document &document,
                                               const Selection &selection);
[[nodiscard]] PlaneCutResult plane_cut(const Document &document,
                                       const PlaneCutRequest &request);
[[nodiscard]] OperationResult
insert_edge_loop(Document &document, Selection &selection, float factor = 0.5F);
[[nodiscard]] OperationResult slice_selected_quads(Document &document,
                                                   Selection &selection,
                                                   int x_slices = 1,
                                                   int y_slices = 1);
[[nodiscard]] OperationResult knife_segment(Document &document,
                                            Selection &selection,
                                            const KnifePointTarget &previous,
                                            const KnifePointTarget &current);
[[nodiscard]] OperationResult
knife_stroke(Document &document, Selection &selection,
             std::span<const KnifePointTarget> points,
             std::span<const KnifeStrokeSegment> segments);
[[nodiscard]] OperationResult
extrude_selected_elements(Document &document, Selection &selection,
                          quader::QVec3 offset,
                          float closed_edge_ledge_size = 0.0F);
[[nodiscard]] OperationResult
transform_extrude_selected_elements(Document &document, Selection &selection,
                                    const Transform3 &transform,
                                    quader::QVec3 pivot);
[[nodiscard]] OperationResult
inset_selected_elements(Document &document, Selection &selection,
                        const Transform3 &transform, quader::QVec3 pivot);
[[nodiscard]] OperationResult inset_selected_faces(Document &document,
                                                   Selection &selection,
                                                   float amount = 0.18F);
[[nodiscard]] OperationResult extrude_selected_faces(Document &document,
                                                     Selection &selection,
                                                     float distance = 0.45F);
[[nodiscard]] OperationResult detach_selected_faces(Document &document,
                                                    Selection &selection);
[[nodiscard]] ExtractFacesResult extract_selected_faces(Document &document,
                                                        Selection &selection);
[[nodiscard]] OperationResult
bridge_selected_faces(Document &document, Selection &selection, int steps = 1);
[[nodiscard]] OperationResult thicken_selected_faces(Document &document,
                                                     Selection &selection,
                                                     float thickness = 0.25F,
                                                     bool from_center = false);

} // namespace quader_poly
