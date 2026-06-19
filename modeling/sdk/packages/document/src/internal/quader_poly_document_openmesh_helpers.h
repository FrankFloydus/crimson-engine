#pragma once

#include "quader_poly_document_topology_backend.h"

namespace quader_poly::document_internal {

using OpenMeshTopologyQuery = DocumentTopologyQuery;

inline bool build_openmesh_topology_query(const Document& document, OpenMeshTopologyQuery& query, std::string& message)
{
    return build_topology_query(document, query, message);
}

inline OperationResult merge_selected_vertices_to_active_with_openmesh(
    Document& document,
    Selection& selection,
    const std::set<ElementId>& merge_vertex_ids,
    ElementId active_vertex_id)
{
    return merge_selected_vertices_to_active_with_topology_backend(document, selection, merge_vertex_ids, active_vertex_id);
}

} // namespace quader_poly::document_internal
