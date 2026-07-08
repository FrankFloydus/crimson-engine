////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <map>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

FacePerimeterInfo face_perimeter_info(const Document& document, ElementId face_id)
{
    const Face* face = find_face(document, face_id);
    if (face == nullptr) {
        return {};
    }

    const std::vector<Edge> perimeter_edges = face_edges(*face);
    return perimeter_info_for_edges(document, perimeter_edges);
}

FacePerimeterInfo selected_faces_perimeter_info(const Document& document, const Selection& selection)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {};
  }

    const std::vector<Face> selected_faces = selected_face_copies(document, selection);
    std::map<std::pair<ElementId, ElementId>, int> selected_edge_counts;
    for (const Face& face : selected_faces) {
        for (const Edge& edge : face_edges(face)) {
            ++selected_edge_counts[{ edge.a, edge.b }];
        }
    }

    std::vector<Edge> perimeter_edges;
    perimeter_edges.reserve(selected_edge_counts.size());
    for (const auto& [edge, selected_count] : selected_edge_counts) {
        if (selected_count == 1) {
            perimeter_edges.push_back({ edge.first, edge.second });
        }
    }

    return perimeter_info_for_edges(document, perimeter_edges);
}

void ensure_face_uvs(Document& document)
{
    for (Face& face : document.faces) {
        if (!face_has_loop_uvs(face)) {
            [[maybe_unused]] const bool assigned = assign_generated_face_uvs(document, face);
        }
    }
}

void clear_face_uvs(Document& document)
{
    for (Face& face : document.faces) {
        face.uvs.clear();
    }
}

} // namespace quader_poly
