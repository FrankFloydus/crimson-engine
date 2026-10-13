////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include "quader_poly_document_bevel_types.hpp"
#include "quader_poly_document_bridge_types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

class VertexLookupIndex;

std::vector<ElementId> selected_valid_vertices(const Document& document, const Selection& selection);
std::vector<Edge> selected_valid_edges(const Document& document, const Selection& selection);
quader::QVec3 document_vertex_centroid(const Document& document);
std::vector<std::size_t> adjacent_face_indices_for_edge(const Document& document, Edge edge);
std::vector<ElementId> loop_between_indices(const std::vector<ElementId>& loop, std::size_t start_index, std::size_t end_index);
std::optional<ConnectEdgeFacePath> shortest_connect_edge_face_path( const Document& document, const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge, Edge first_edge, Edge second_edge, const std::set<std::pair<ElementId, ElementId>>* blocked_edges);
void add_connect_path_edges( const Document& document, const ConnectEdgeFacePath& path, Edge first_edge, Edge second_edge, std::map<ElementId, std::vector<Edge>>& cut_edges_by_face_id);
std::vector<ConnectEdgeFaceRegion> connect_edge_face_regions( const Document& document, const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge, const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys);
void add_unordered_connect_region_paths( const Document& document, const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge, const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys, const ConnectEdgeFaceRegion& region, std::map<ElementId, std::vector<Edge>>& cut_edges_by_face_id);
ElementId duplicate_vertex_for_face( Document& document, std::map<std::pair<ElementId, ElementId>, ElementId>& duplicate_vertices, ElementId face_id, ElementId vertex_id);
std::optional<std::vector<ElementId>> closed_edge_loop_from_edges(std::span<const Edge> selected_edges);
bool orient_face_against_adjacent_winding(const Document& document, Face& face);
const Face* face_for_open_edge(const Document& document, Edge edge);
std::uint32_t material_slot_for_open_edge(const Document& document, Edge edge);
ElementId add_face_with_texture_transform(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot, const FaceTextureTransform& texture_transform);
bool inherit_merged_face_texture(const Document& source_document, std::span<const ElementId> source_face_ids, Face& merged_face);
bool assign_merged_face_texture_boundary(const Document& source_document,
    std::span<const ElementId> source_face_ids, Face& merged_face);
bool merged_face_preserves_source_textures(const Document& source_document,
    std::span<const ElementId> source_face_ids, const Face& merged_face);
// Requires the compiled triangle boundary to follow the complete 3D face perimeter;
// triangles may span omitted corners only along an ordered straight boundary segment.
bool face_triangulation_preserves_boundary(const Document& document, const Face& face,
    const VertexLookupIndex& vertex_lookup);
std::vector<ElementId> face_vertices_between(const Face& face, std::size_t start_index, std::size_t end_index);
std::vector<ElementId> unique_valid_face_loop(std::vector<ElementId> vertices);
std::optional<std::size_t> directed_face_edge_index(const Face& face, ElementId from_id, ElementId to_id);
std::optional<std::pair<ElementId, ElementId>> oriented_edge_in_face(const Face& face, Edge edge);
std::optional<OpenBridgeEdgeInfo> open_bridge_edge_info( const Document& document, Edge edge, const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts);
quader::QVec3 bridge_edge_outward_direction(const Document& document, const OpenBridgeEdgeInfo& edge_info);
/** Retains the accepted cubic coefficients shared by rail geometry and guide.
 */
struct BridgeRailCurve {
  quader::QVec3 start{};
  quader::QVec3 end{};
  quader::QVec3 start_tangent{};
  quader::QVec3 end_tangent{};
  quader::QVec3 start_handle_axis{};
  quader::QVec3 end_handle_axis{};
  bool linear = true;
};
std::optional<BridgeOptions> normalized_bridge_options(BridgeOptions options);
BridgeRailCurve prepare_bridge_rail_curve(quader::QVec3 start,
                                          quader::QVec3 end,
                                          quader::QVec3 start_outward,
                                          quader::QVec3 end_outward,
                                          const BridgeOptions &options);
quader::QVec3 evaluate_bridge_rail_curve(const BridgeRailCurve &curve,
                                         int step_index, int step_count);
BridgeShapeFeedback
bridge_shape_feedback(std::span<const BridgeRailCurve> curves,
                      const BridgeOptions &options);
std::vector<ElementId> unique_bridge_vertices(Edge first_edge,
                                              Edge second_edge);
bool face_contains_reversed_bridge_edge(const Face& face, const OpenBridgeEdgeInfo& edge_info);
std::optional<std::vector<ElementId>> bridge_face_loop_from_open_edges( const Document& document, const OpenBridgeEdgeInfo& first_edge, const OpenBridgeEdgeInfo& second_edge);
std::tuple<ElementId, ElementId, ElementId> edge_bevel_side_key(ElementId face_id, Edge edge);
bool edge_bevel_edge_is_concave(const Document& document, Edge edge, std::span<const std::size_t> adjacent_faces);
EdgeBevelSettings sanitized_edge_bevel_settings(EdgeBevelSettings settings);
float record_edge_bevel_applied_width(EdgeBevelWidthFeedback *feedback, float requested_width, float applied_width);
float safe_edge_bevel_width_for_face_edge(const Document& document, const Face& face, std::size_t edge_index, float requested_width, EdgeBevelWidthFeedback *feedback = nullptr);
std::optional<EdgeBevelSide> edge_bevel_side_for_face( Document& candidate, const Document& source, Edge edge, std::size_t face_index, float width, EdgeBevelWidthFeedback *feedback = nullptr);
ElementId edge_bevel_side_endpoint_vertex(const EdgeBevelSide& side, ElementId endpoint_id);
quader::QVec3 edge_bevel_side_endpoint_position(const EdgeBevelSide& side, ElementId endpoint_id);
std::vector<ElementId> compact_edge_bevel_face_loop(std::vector<ElementId> vertices);
std::optional<EdgeBevelOffsetLine> edge_bevel_offset_line_for_face_edge( const Document& document, const Face& face, std::size_t edge_index, float width, EdgeBevelWidthFeedback *feedback = nullptr);
std::optional<quader::QVec3> edge_bevel_intersect_offset_lines(const EdgeBevelOffsetLine& first, const EdgeBevelOffsetLine& second);
std::optional<std::pair<ElementId, quader::QVec3>> edge_bevel_face_vertex_miter( Document& candidate, const Document& source, const Face& face, ElementId vertex_id, float width, const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys, std::map<std::pair<ElementId, ElementId>, std::pair<ElementId, quader::QVec3>>& miter_vertices, EdgeBevelWidthFeedback *feedback = nullptr);
ElementId edge_bevel_profile_vertex_for_source( Document& candidate, ElementId source_vertex_id, quader::QVec3 position, std::map<ElementId, std::vector<std::pair<ElementId, quader::QVec3>>>& profile_vertices_by_source);
quader::QVec3 edge_bevel_profile_middle_on_edge(quader::QVec3 edge_start, quader::QVec3 edge_end, quader::QVec3 first_boundary, quader::QVec3 second_boundary);
bool append_edge_bevel_face( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal, const FaceTextureTransform& texture_transform = {});
void add_unique_edge_bevel_patch_vertex(std::vector<std::pair<ElementId, quader::QVec3>>& points, ElementId vertex_id, quader::QVec3 position);
std::vector<ElementId> ordered_edge_bevel_patch_vertices( const Document& document, ElementId source_vertex_id, std::vector<std::pair<ElementId, quader::QVec3>> points);
bool edge_bevel_point_lies_on_face_plane(const Document& document, const Face& face, quader::QVec3 position);
bool edge_bevel_point_lies_in_face_corner( const Document& document, const Face& face, ElementId vertex_id, ElementId previous_id, ElementId next_id, quader::QVec3 position);
std::vector<ElementId> edge_bevel_endpoint_profile_vertices(const EdgeBevelBuild& build, ElementId endpoint_id);
bool edge_bevel_endpoint_profile_lies_in_face_corner( const Document& document, const Face& face, ElementId vertex_id, ElementId previous_id, ElementId next_id, std::span<const ElementId> profile_vertices);
void append_edge_bevel_terminal_profile_vertices( const Document& document, ElementId vertex_id, ElementId previous_id, ElementId next_id, std::span<const ElementId> profile_vertices, std::vector<ElementId>& rebuilt_loop);
void append_edge_bevel_unselected_vertex_offsets( const Document& document, const Face& face, ElementId vertex_id, ElementId previous_id, ElementId next_id, std::span<const EdgeBevelFaceVertexOffset> offsets, std::vector<ElementId>& rebuilt_loop);
std::vector<ElementId> merged_face_loop_for_dissolved_edge(const Face& first_face, const Face& second_face, ElementId first_from, ElementId first_to);
std::optional<std::size_t> loop_vertex_index(std::span<const ElementId> loop, ElementId vertex_id);
bool loop_vertex_is_redundant(const Document& document, std::span<const ElementId> loop, std::size_t index);
bool loop_vertex_is_redundant(const Document& document, std::span<const ElementId> loop, ElementId vertex_id);
void append_loop_neighbors_for_vertex(std::span<const ElementId> loop, ElementId vertex_id, std::vector<ElementId>& neighbors);
bool vertex_is_redundant_in_every_face_that_uses_it(const Document& document, ElementId vertex_id);
std::vector<ElementId> remove_redundant_vertices_from_loop( const Document& document, std::vector<ElementId> loop, const std::set<ElementId>& vertices_to_remove, std::vector<ElementId>& removed_vertices);
bool remove_redundant_vertex_from_all_face_loops(Document& document, ElementId vertex_id);
void orient_face_toward_normal(const Document& document, Face& face, quader::QVec3 expected_normal);
/// Selects the loop anchor that preserves tessellation for a winding change.
enum class FaceWindingChange {
    OrientationOnly,
    ReflectedGeometry,
};
void reverse_face_winding(Face& face,
    FaceWindingChange change = FaceWindingChange::OrientationOnly);
// Reverses an affine-reflected face without reinterpreting its authored chart.
// Failure leaves result_face unchanged when neither existing anchor can retain
// the source triangulation and its chart is not a planar affine mapping.
bool reverse_reflected_face_winding(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face);
bool reverse_reflected_face_winding(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face,
    const VertexLookupIndex& source_lookup, const VertexLookupIndex& result_lookup);
/// States whether an affine rebase also needs to reverse the face orientation.
enum class FaceRebaseWinding {
    Preserve,
    Reverse,
};
// Retains authored n-gon triangle identities across an affine rebase, trying
// only the current and adjacent loop anchors. Affine charts need no fixed
// triangulation. Failure leaves result_face unchanged.
bool preserve_rebased_face_texture_triangulation(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face,
    FaceRebaseWinding winding, const VertexLookupIndex& source_lookup,
    const VertexLookupIndex& result_lookup);
std::vector<Face> selected_face_copies(const Document& document, const Selection& selection);
ElementId copied_vertex_id( const Document& source, Document& target, std::map<ElementId, ElementId>& vertex_map, ElementId source_vertex_id);
bool append_copied_face( const Document& source, Document& target, const Face& source_face, std::map<ElementId, ElementId>& vertex_map, std::map<ElementId, ElementId>& face_map, Selection& target_selection);
std::vector<FaceIslandBoundary> selected_face_island_boundaries(const Document& document, const std::vector<Face>& selected_faces);
float loop_alignment_score( const Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices);
std::vector<ElementId> aligned_loop_vertices( const Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices);
quader::QVec3 vertex_position_or_zero(const Document& document, ElementId vertex_id);
quader::QVec3 vertex_loop_centroid(const Document& document, std::span<const ElementId> vertex_ids);
bool append_bridge_face(Document& document, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal, std::vector<ElementId>& bridge_face_ids, const FaceTextureTransform& texture_transform = {});
bool append_bridge_faces_between_loops( Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices, quader::QVec3 first_normal, quader::QVec3 second_normal, std::uint32_t material_slot, BridgeOptions options, std::vector<ElementId>& bridge_face_ids, bool skip_degenerate_faces, const FaceTextureTransform& texture_transform, BridgeShapeFeedback& feedback);
quader::QVec3 face_centroid(const Document& document, const Face& face);
} // namespace quader_poly::document_internal
