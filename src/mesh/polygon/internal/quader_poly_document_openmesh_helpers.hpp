////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "quader_poly_document_topology_backend.hpp"

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
