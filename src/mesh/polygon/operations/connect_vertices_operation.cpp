////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Connect Vertices Operation modeling operation for the polygon document and mesh editing core.
 */
class ConnectVerticesOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedVertices).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ConnectSelectedVertices).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

OperationResult ConnectVerticesOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex) {
    return {false, "Connect needs vertex selection mode."};
  }

    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.size() < 2) {
        return { false, "Select at least two vertices to connect." };
    }

    const Document source = document;
    for (std::size_t face_index = 0; face_index < source.faces.size(); ++face_index) {
        const Face& face = source.faces[face_index];
        std::vector<std::size_t> selected_indices;
        selected_indices.reserve(selected_vertices.size());
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            if (contains_id(selected_vertices, face.vertices[index])) {
                selected_indices.push_back(index);
            }
        }
        if (selected_indices.size() != selected_vertices.size()) {
            continue;
        }

        std::vector<Face> replacement_faces;
        replacement_faces.reserve(selected_indices.size() + 1U);
        std::vector<Edge> connected_edges;
        connected_edges.reserve(selected_indices.size());
        ElementId next_face_id = source.next_face_id;
        if (selected_indices.size() == 2) {
            const std::size_t first = selected_indices[0];
            const std::size_t second = selected_indices[1];
            std::vector<ElementId> first_loop = unique_valid_face_loop(face_vertices_between(face, first, second));
            std::vector<ElementId> second_loop = unique_valid_face_loop(face_vertices_between(face, second, first));
            if (first_loop.size() < 3 || second_loop.size() < 3) {
                continue;
            }

            Face first_face;
            first_face.id = face.id;
            first_face.vertices = std::move(first_loop);
            first_face.material_slot = face.material_slot;
            replacement_faces.push_back(std::move(first_face));

            Face second_face;
            second_face.id = next_face_id++;
            second_face.vertices = std::move(second_loop);
            second_face.material_slot = face.material_slot;
            replacement_faces.push_back(std::move(second_face));
            connected_edges.push_back(make_edge(face.vertices[first], face.vertices[second]));
        } else {
            std::vector<ElementId> center_loop;
            center_loop.reserve(selected_indices.size());
            for (const std::size_t selected_index : selected_indices) {
                center_loop.push_back(face.vertices[selected_index]);
            }
            center_loop = unique_valid_face_loop(std::move(center_loop));
            if (center_loop.size() < 3) {
                continue;
            }

            Face center_face;
            center_face.id = face.id;
            center_face.vertices = std::move(center_loop);
            center_face.material_slot = face.material_slot;
            replacement_faces.push_back(std::move(center_face));

            for (std::size_t index = 0; index < selected_indices.size(); ++index) {
                const std::size_t start = selected_indices[index];
                const std::size_t end = selected_indices[(index + 1U) % selected_indices.size()];
                add_unique_edge(connected_edges, make_edge(face.vertices[start], face.vertices[end]));
                std::vector<ElementId> side_loop = unique_valid_face_loop(face_vertices_between(face, start, end));
                if (side_loop.size() < 3) {
                    continue;
                }

                Face side_face;
                side_face.id = next_face_id++;
                side_face.vertices = std::move(side_loop);
                side_face.material_slot = face.material_slot;
                replacement_faces.push_back(std::move(side_face));
            }
        }

        if (replacement_faces.size() < 2 || connected_edges.empty()) {
            continue;
        }

        Document candidate = source;
        candidate.next_face_id = next_face_id;
        candidate.faces.erase(candidate.faces.begin() + static_cast<std::ptrdiff_t>(face_index));
        candidate.faces.insert(candidate.faces.end(), replacement_faces.begin(), replacement_faces.end());
        prune_invalid_faces(candidate);
        if (!every_face_triangulates(candidate)) {
            continue;
        }

        document = std::move(candidate);

        selection.clear();
        selection.mode = SelectionMode::Edge;
        selection.edges = std::move(connected_edges);
        activate_last_selection(selection);
        return { true, {} };
    }

    return { false, "Connect needs selected vertices on the same face." };
}

} // namespace

OperationResult connect_selected_vertices(Document& document, Selection& selection)
{
    return ConnectVerticesOperation().apply(document, selection);
}

} // namespace quader_poly
