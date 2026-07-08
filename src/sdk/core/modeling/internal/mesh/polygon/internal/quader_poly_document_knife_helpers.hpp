////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "quader_poly_document_knife_types.hpp"

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

ElementId other_edge_vertex(Edge edge, ElementId vertex);
bool edge_exists(const Document& document, Edge edge);
bool edges_share_face(const Document& document, Edge left, Edge right);
std::vector<Edge> incident_edges(const Document& document, ElementId vertex_id);
std::optional<Edge> next_edge_loop_edge(const Document& document, Edge current, ElementId through_vertex);
void append_edge_loop_direction(const Document& document, Edge seed, ElementId through_vertex, std::vector<Edge>& edges);
std::optional<std::size_t> face_edge_index(const Face& face, Edge edge);
Edge directed_face_edge(const Face& face, std::size_t edge_index);
bool same_directed_edge(Edge left, Edge right);
Edge oriented_loop_opposite_edge(const Face& face, std::size_t entry_index, Edge entry_edge);
const Face* find_face_copy(std::span<const Face> faces, ElementId id);
std::vector<EdgeLoopFaceSplit> collect_edge_loop_splits(const Document& document, Edge seed_edge);
std::optional<quader::QVec3> split_edge_position(const Document& document, Edge edge, float factor);
std::pair<ElementId, ElementId> edge_key(Edge edge);
ElementId split_vertex_for_edge( Document& document, std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices, Edge edge, float factor);
bool knife_edge_splits_empty(const KnifeEdgeSplitMap& split_vertices);
std::size_t knife_edge_split_count(const KnifeEdgeSplitMap& split_vertices);
float knife_edge_factor_for_key(Edge edge, float factor);
ElementId split_vertex_for_knife_edge( Document& document, KnifeEdgeSplitMap& split_vertices, Edge edge, float factor);
ElementId split_vertex_near_endpoint( Document& document, std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices, ElementId endpoint_id, ElementId neighbor_id, float distance);
bool knife_target_is_edge(const KnifePointTarget& target);
bool knife_targets_use_same_edge(const KnifePointTarget& left, const KnifePointTarget& right);
std::optional<ResolvedKnifeTarget> resolve_knife_target( Document& candidate, std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices, const KnifePointTarget& target, std::string& message);
bool apply_knife_edge_splits_to_faces(Document& candidate, const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices);
bool apply_knife_edge_splits_to_faces(Document& candidate, const KnifeEdgeSplitMap& split_vertices);
KnifePoint2 knife_project_point(quader::QVec3 position, int dropped_axis);
KnifePoint2 knife_project_vertex(const Document& document, ElementId vertex_id, int dropped_axis);
double knife_distance_squared_2d(const KnifePoint2& a, const KnifePoint2& b);
bool knife_point_on_segment_2d(const KnifePoint2& point, const KnifePoint2& a, const KnifePoint2& b);
bool knife_segments_intersect_2d(const KnifePoint2& a, const KnifePoint2& b, const KnifePoint2& c, const KnifePoint2& d);
double knife_signed_area_2d(std::span<const KnifePoint2> points);
std::vector<KnifePoint2> knife_face_projected_loop(const Document& document, const Face& face, int dropped_axis);
bool knife_point_in_or_on_polygon_2d(const KnifePoint2& point, std::span<const KnifePoint2> polygon);
bool knife_vertex_is_on_face_boundary(const Face& face, ElementId vertex_id);
std::optional<KnifeBoundaryTarget> knife_face_boundary_target_at_position( const Document& document, const Face& face, quader::QVec3 position, int dropped_axis);
bool knife_resolved_point_lies_on_face(const Face& face, const KnifeResolvedStrokePoint& point);
bool knife_segment_crosses_existing_edges( const Document& document, const Face& face, Edge segment, std::span<const Edge> cut_edges, int dropped_axis);
bool knife_segment_stays_inside_face( const Document& document, const Face& face, Edge segment, int dropped_axis, std::span<const KnifePoint2> projected_face);
std::optional<Edge> knife_best_connector_to_boundary( const Document& document, const Face& face, ElementId interior_vertex, std::span<const Edge> cut_edges, int dropped_axis, const std::set<ElementId>& reserved_boundary_vertices );
bool knife_add_connector_to_boundary( const Document& document, const Face& face, ElementId interior_vertex, std::vector<Edge>& cut_edges, int dropped_axis, const std::set<ElementId>& reserved_boundary_vertices );
bool knife_add_component_connectors( const Document& document, const Face& face, std::vector<Edge>& cut_edges, int dropped_axis, std::string& message);
bool knife_validate_cut_edges_for_face( const Document& document, const Face& face, std::span<const Edge> cut_edges, int dropped_axis, std::string& message);
std::optional<KnifePoint2> knife_proper_segment_intersection_2d( const KnifePoint2& a, const KnifePoint2& b, const KnifePoint2& c, const KnifePoint2& d);
quader::QVec3 knife_unproject_face_point(const Document& document, const Face& face, int dropped_axis, const KnifePoint2& point);
ElementId knife_vertex_at_position(Document& document, quader::QVec3 position, float tolerance);
void knife_split_cut_edge_intersections(Document& document, const Face& face, std::vector<Edge>& cut_edges, int dropped_axis);
bool knife_face_loop_is_valid(const Document& document, std::span<const ElementId> loop);
void knife_orient_loop_like_source(const Document& document, const Face& source_face, std::vector<ElementId>& loop, int dropped_axis);
std::vector<std::vector<ElementId>> knife_partition_open_chain_face_loops( const Document& document, const Face& face, std::span<const Edge> cut_edges, int dropped_axis);
std::vector<std::vector<ElementId>> knife_partition_face_loops_for_turn( const Document& document, const Face& face, std::span<const Edge> cut_edges, int dropped_axis, bool previous_turn);
std::set<ElementId> knife_required_cut_vertices(const Face& face, std::span<const Edge> cut_edges);
std::size_t knife_loop_vertex_coverage( std::span<const std::vector<ElementId>> loops, const std::set<ElementId>& required_vertices);
bool knife_loops_cover_required_vertices( std::span<const std::vector<ElementId>> loops, const std::set<ElementId>& required_vertices);
std::vector<ElementId> knife_uncovered_required_vertices( std::span<const std::vector<ElementId>> loops, const std::set<ElementId>& required_vertices);
std::vector<std::vector<ElementId>> knife_partition_face_loops( const Document& document, const Face& face, std::span<const Edge> cut_edges, int dropped_axis);
std::vector<std::vector<ElementId>> knife_partition_face_loops_with_repairs( Document& document, const Face& face, std::vector<Edge>& cut_edges, int dropped_axis, std::string& message);
std::optional<ElementId> knife_segment_face_id( const Document& document, const std::vector<KnifeResolvedStrokePoint>& points, const KnifeStrokeSegment& segment);
std::vector<Edge> knife_shared_face_edges(const Face& first, const Face& second);
bool knife_vertex_is_on_shared_face_boundary(const Face& first, const Face& second, ElementId vertex_id);
float knife_edge_factor_nearest_segment( const Document& document, Edge edge, quader::QVec3 segment_start, quader::QVec3 segment_end);
float knife_distance_squared_to_segment(quader::QVec3 point, quader::QVec3 segment_start, quader::QVec3 segment_end);
bool knife_add_face_graph_edge( KnifeStrokeCandidate& build, std::map<ElementId, KnifeFaceGraph>& face_graphs, ElementId face_id, Edge edge);
bool knife_face_graph_uses_vertex(const KnifeFaceGraph& graph, ElementId vertex_id);
bool knife_any_face_graph_uses_vertex(const std::map<ElementId, KnifeFaceGraph>& face_graphs, ElementId vertex_id);
bool knife_ensure_point_connected_to_face_graph( Document& document, std::map<ElementId, KnifeFaceGraph>& face_graphs, const KnifeResolvedStrokePoint& point, std::string& message);
KnifeStrokeCandidate build_knife_stroke_candidate( const Document& document, std::span<const KnifePointTarget> targets, std::span<const KnifeStrokeSegment> stroke_segments);
bool face_indices_are_adjacent(const Face& face, std::size_t first, std::size_t second);
std::vector<std::size_t> splittable_knife_face_indices(const Document& document, ElementId previous_vertex, ElementId current_vertex);
KnifeSegmentCandidate build_knife_segment_candidate( const Document& document, const KnifePointTarget& previous, const KnifePointTarget& current);
} // namespace quader_poly::document_internal
