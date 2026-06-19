#pragma once

#include "quader_poly_document_hull_types.h"

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

bool points_nearly_equal(Vec3 left, Vec3 right, float tolerance);
bool append_unique_hull_plane(std::vector<Plane3>& planes, Plane3 plane);
std::vector<HullVertex> unique_hull_vertices_from_document(const Document& document);
std::vector<Plane3> build_hull_planes_from_vertices(std::span<const HullVertex> vertices);
ElementId hull_vertex_id_for_position(std::span<const HullVertex> source_vertices, Vec3 position);
const HullVertex* find_hull_vertex_by_id(const HullGeometry& geometry, ElementId id);
Vec3 hull_position_for_id(const HullGeometry& geometry, ElementId id);
void sort_hull_face_vertices(const HullGeometry& geometry, HullFace& face);
bool hull_has_equivalent_face(const HullGeometry& geometry, const Plane3& plane);
bool build_convex_hull_geometry(std::span<const HullVertex> source_vertices, HullGeometry& geometry);
std::vector<ElementId> sorted_face_vertex_ids(std::span<const ElementId> vertex_ids);
bool document_matches_convex_hull_shape(const Document& document, const HullGeometry& hull);
std::optional<std::pair<HullFace, HullFace>> split_hull_face_by_edge(const HullFace& face, Edge edge);
std::vector<Edge> source_split_edges_for_hull_face( const Document& source_document, const HullFace& hull_face, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id);
std::vector<HullFace> split_hull_face_by_source_edges( const Document& source_document, const HullFace& hull_face, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id);
Document document_from_hull_geometry( const Document& source_document, const HullGeometry& hull, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id);
bool document_is_simple_convex_hull(const Document& document);
bool build_convex_hull_vertex_merge_candidate( const Document& document, Document& candidate, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id, Vec3 target_position);
bool every_face_triangulates(const Document& document);
ElementId mapped_vertex_for_merge(ElementId vertex_id, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id);
float face_loop_area_score(const Document& document, std::span<const ElementId> vertex_ids);
bool face_loop_is_valid_after_merge(const Document& document, std::span<const ElementId> vertex_ids);
Face make_repaired_merge_face(const Face& source_face, std::vector<ElementId> vertices);
std::vector<Face> repaired_faces_for_vertex_merge( const Document& source_document, const Document& candidate_document, const Face& source_face, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id);
OperationResult build_vertex_merge_candidate( const Document& document, Document& candidate, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id, Vec3 target_position);
void prune_unused_vertices(Document& document);
void prune_invalid_faces(Document& document);
} // namespace quader_poly::document_internal
