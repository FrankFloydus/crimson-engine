////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

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
std::uint32_t material_slot_for_open_edge(const Document& document, Edge edge);
std::vector<ElementId> face_vertices_between(const Face& face, std::size_t start_index, std::size_t end_index);
std::vector<ElementId> unique_valid_face_loop(std::vector<ElementId> vertices);
std::optional<std::size_t> directed_face_edge_index(const Face& face, ElementId from_id, ElementId to_id);
std::optional<std::pair<ElementId, ElementId>> oriented_edge_in_face(const Face& face, Edge edge);
std::optional<OpenBridgeEdgeInfo> open_bridge_edge_info( const Document& document, Edge edge, const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts);
quader::QVec3 bridge_edge_outward_direction(const Document& document, const OpenBridgeEdgeInfo& edge_info);
quader::QVec3 curved_bridge_position( quader::QVec3 start, quader::QVec3 end, quader::QVec3 start_outward, quader::QVec3 end_outward, int step_index, int step_count);
std::vector<ElementId> unique_bridge_vertices(Edge first_edge, Edge second_edge);
bool face_contains_reversed_bridge_edge(const Face& face, const OpenBridgeEdgeInfo& edge_info);
std::optional<std::vector<ElementId>> bridge_face_loop_from_open_edges( const Document& document, const OpenBridgeEdgeInfo& first_edge, const OpenBridgeEdgeInfo& second_edge);
std::tuple<ElementId, ElementId, ElementId> edge_bevel_side_key(ElementId face_id, Edge edge);
bool edge_bevel_edge_is_concave(const Document& document, Edge edge, std::span<const std::size_t> adjacent_faces);
EdgeBevelSettings sanitized_edge_bevel_settings(EdgeBevelSettings settings);
float edge_bevel_shape_profile(float profile);
float safe_edge_bevel_width_for_face_edge(const Document& document, const Face& face, std::size_t edge_index, float requested_width);
std::optional<EdgeBevelSide> edge_bevel_side_for_face( Document& candidate, const Document& source, Edge edge, std::size_t face_index, float width);
ElementId edge_bevel_side_endpoint_vertex(const EdgeBevelSide& side, ElementId endpoint_id);
quader::QVec3 edge_bevel_side_endpoint_position(const EdgeBevelSide& side, ElementId endpoint_id);
std::vector<ElementId> compact_edge_bevel_face_loop(std::vector<ElementId> vertices);
std::optional<EdgeBevelOffsetLine> edge_bevel_offset_line_for_face_edge( const Document& document, const Face& face, std::size_t edge_index, float width);
std::optional<quader::QVec3> edge_bevel_intersect_offset_lines(const EdgeBevelOffsetLine& first, const EdgeBevelOffsetLine& second);
std::optional<std::pair<ElementId, quader::QVec3>> edge_bevel_face_vertex_miter( Document& candidate, const Document& source, const Face& face, ElementId vertex_id, float width, const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys, std::map<std::pair<ElementId, ElementId>, std::pair<ElementId, quader::QVec3>>& miter_vertices);
ElementId edge_bevel_profile_vertex_for_source( Document& candidate, ElementId source_vertex_id, quader::QVec3 position, std::map<ElementId, std::vector<std::pair<ElementId, quader::QVec3>>>& profile_vertices_by_source);
float edge_bevel_superellipse_exponent(float profile);
double edge_bevel_superellipse_y(double x, float exponent);
std::pair<float, float> edge_bevel_square_profile_coordinates(int segment, int segments, bool inward);
std::vector<EdgeBevelProfilePolylinePoint> edge_bevel_build_superellipse_profile_polyline(int segments, float exponent);
std::pair<float, float> edge_bevel_sample_profile_polyline(std::span<const EdgeBevelProfilePolylinePoint> samples, float fraction);
std::pair<float, float> edge_bevel_profile_coordinates(int segment, int segments, float profile, BevelProfileType profile_type);
quader::QVec3 edge_bevel_profile_point(quader::QVec3 source_position, quader::QVec3 first_boundary, quader::QVec3 second_boundary, int segment, int segments, float profile, BevelProfileType profile_type);
quader::QVec3 edge_bevel_profile_middle_on_edge(quader::QVec3 edge_start, quader::QVec3 edge_end, quader::QVec3 first_boundary, quader::QVec3 second_boundary);
void edge_bevel_set_component(quader::QVec3& value, int axis, float component);
quader::QVec3 edge_bevel_snap_to_superellipsoid(quader::QVec3 point, float exponent);
quader::QVec3 edge_bevel_map_unit_cube_corner(quader::QVec3 axis0, quader::QVec3 axis1, quader::QVec3 axis2, quader::QVec3 source, quader::QVec3 point);
bool append_edge_bevel_face( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal);
EdgeBevelTriCornerKey edge_bevel_tri_corner_canonical_key(int side, int ring, int segment, int segments);
std::optional<std::vector<ElementId>> ordered_tri_corner_boundary_ring( const Document& document, ElementId source_vertex_id, std::vector<ElementId> patch_vertices, int segments);
bool append_edge_bevel_tri_corner_patch_faces( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, const std::vector<ElementId>& patch_vertices, ElementId source_vertex_id, std::uint32_t material_slot, quader::QVec3 expected_normal, int segments, float profile, BevelProfileType profile_type);
int edge_bevel_positive_mod(int value, int divisor);
EdgeBevelVMeshKey edge_bevel_vmesh_canonical_key(int side, int ring, int segment, int side_count, int segments);
bool edge_bevel_vmesh_is_canonical(const EdgeBevelVMesh& vmesh, int side, int ring, int segment);
std::size_t edge_bevel_vmesh_slot_index(const EdgeBevelVMesh& vmesh, int side, int ring, int segment);
EdgeBevelVMeshSlot& edge_bevel_vmesh_slot(EdgeBevelVMesh& vmesh, int side, int ring, int segment);
const EdgeBevelVMeshSlot& edge_bevel_vmesh_slot(const EdgeBevelVMesh& vmesh, int side, int ring, int segment);
EdgeBevelVMeshSlot& edge_bevel_vmesh_canonical_slot(EdgeBevelVMesh& vmesh, int side, int ring, int segment);
const EdgeBevelVMeshSlot& edge_bevel_vmesh_canonical_slot(const EdgeBevelVMesh& vmesh, int side, int ring, int segment);
EdgeBevelVMesh edge_bevel_make_vmesh(int side_count, int segments, std::vector<EdgeBevelVMeshProfile> profiles);
quader::QVec3 edge_bevel_vmesh_profile_point( const EdgeBevelVMesh& vmesh, int side, int segment, int segment_count, float profile, BevelProfileType profile_type);
void edge_bevel_vmesh_copy_equivalent_slots(EdgeBevelVMesh& vmesh);
quader::QVec3 edge_bevel_average4(quader::QVec3 a, quader::QVec3 b, quader::QVec3 c, quader::QVec3 d);
float edge_bevel_sabin_gamma(int side_count);
float edge_bevel_profile_fullness(int segments, float profile);
std::vector<float> edge_bevel_vmesh_boundary_fractions(const EdgeBevelVMesh& vmesh, int side);
std::vector<float> edge_bevel_vmesh_profile_fractions( const EdgeBevelVMesh& vmesh, int side, int segments, float profile, BevelProfileType profile_type);
int edge_bevel_vmesh_interp_range(std::span<const float> fractions, float value, float& rest);
quader::QVec3 edge_bevel_bilinear(quader::QVec3 lower_left, quader::QVec3 lower_right, quader::QVec3 upper_right, quader::QVec3 upper_left, float x, float y);
EdgeBevelVMesh edge_bevel_vmesh_cubic_subdiv( EdgeBevelVMesh vmesh_in, float profile, BevelProfileType profile_type);
quader::QVec3 edge_bevel_vmesh_center(const EdgeBevelVMesh& vmesh);
EdgeBevelVMesh edge_bevel_vmesh_interpolate( const EdgeBevelVMesh& vmesh_in, int target_segments, float profile, BevelProfileType profile_type);
std::vector<EdgeBevelVMeshProfile> edge_bevel_tri_cube_corner_profiles();
EdgeBevelVMesh edge_bevel_make_tri_cube_corner_square_vmesh(int segments);
EdgeBevelVMesh edge_bevel_make_tri_cube_corner_vmesh(int segments, float profile, BevelProfileType profile_type);
bool append_edge_bevel_vmesh_faces( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, EdgeBevelVMesh& vmesh, std::uint32_t material_slot, quader::QVec3 expected_normal, std::optional<quader::QVec3> orientation_source, bool orient_away_from_source);
bool edge_bevel_vertices_match(const Document& document, ElementId first_id, ElementId second_id);
std::optional<std::vector<EdgeBevelCornerArc>> ordered_edge_bevel_corner_arcs( const Document& document, std::vector<EdgeBevelCornerArc> arcs, int segments);
bool append_edge_bevel_tri_corner_arc_patch_faces( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, const std::vector<EdgeBevelCornerArc>& ordered_arcs, ElementId source_vertex_id, std::uint32_t material_slot, quader::QVec3 expected_normal, int segments, float profile);
bool append_edge_bevel_vmesh_corner_patch_faces( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, std::vector<EdgeBevelCornerArc> arcs, ElementId source_vertex_id, std::uint32_t material_slot, quader::QVec3 expected_normal, int segments, float profile, BevelProfileType profile_type);
bool append_edge_bevel_corner_patch_faces( Document& document, std::vector<Face>& faces, std::vector<ElementId>& generated_face_ids, std::vector<ElementId> patch_vertices, ElementId source_vertex_id, int selected_edge_count, std::uint32_t material_slot, quader::QVec3 expected_normal, int segments, float profile, BevelProfileType profile_type);
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
void reverse_face_winding(Face& face);
std::vector<Face> selected_face_copies(const Document& document, const Selection& selection);
ElementId copied_vertex_id( const Document& source, Document& target, std::map<ElementId, ElementId>& vertex_map, ElementId source_vertex_id);
bool append_copied_face( const Document& source, Document& target, const Face& source_face, std::map<ElementId, ElementId>& vertex_map, std::map<ElementId, ElementId>& face_map, Selection& target_selection);
std::vector<FaceIslandBoundary> selected_face_island_boundaries(const Document& document, const std::vector<Face>& selected_faces);
float loop_alignment_score( const Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices);
std::vector<ElementId> aligned_loop_vertices( const Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices);
quader::QVec3 vertex_position_or_zero(const Document& document, ElementId vertex_id);
quader::QVec3 vertex_loop_centroid(const Document& document, std::span<const ElementId> vertex_ids);
bool append_bridge_face(Document& document, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal, std::vector<ElementId>& bridge_face_ids);
bool append_bridge_faces_between_loops( Document& document, std::span<const ElementId> first_vertices, std::span<const ElementId> second_vertices, quader::QVec3 first_normal, quader::QVec3 second_normal, std::uint32_t material_slot, int steps, std::vector<ElementId>& bridge_face_ids, bool skip_degenerate_faces);
quader::QVec3 face_centroid(const Document& document, const Face& face);
} // namespace quader_poly::document_internal
