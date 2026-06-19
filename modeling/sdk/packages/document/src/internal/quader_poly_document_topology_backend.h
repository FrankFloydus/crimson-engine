#pragma once

#include <quader_poly/document.h>

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

struct DocumentTopologyQuery {
    std::map<std::pair<ElementId, ElementId>, int> edge_incidence_counts;
    std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> face_indices_by_edge;
    std::map<ElementId, std::vector<ElementId>> adjacent_vertices_by_vertex;
    std::map<ElementId, std::vector<std::size_t>> face_indices_by_vertex;
    std::vector<Edge> boundary_edges;
    std::vector<Edge> closed_edges;
    std::vector<Edge> nonmanifold_edges;
};

using TopologyQuery = DocumentTopologyQuery;

bool build_topology_query(const Document& document, DocumentTopologyQuery& query, std::string& message);
OperationResult merge_selected_vertices_to_active_with_topology_backend(
    Document& document,
    Selection& selection,
    const std::set<ElementId>& merge_vertex_ids,
    ElementId active_vertex_id);

} // namespace quader_poly::document_internal
