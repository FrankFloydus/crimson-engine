////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <cstddef>
#include <set>

namespace quader_poly {

ElementId add_vertex(Document& document, quader::QVec3 position)
{
    const ElementId id = document.next_vertex_id++;
    document.vertices.push_back({ id, position });
    return id;
}

ElementId add_face(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot)
{
    if (vertices.size() < 3) {
      return kInvalidElementId;
    }

    const ElementId id = document.next_face_id++;
    Face face;
    face.id = id;
    face.vertices.assign(vertices.begin(), vertices.end());
    face.material_slot = material_slot;
    document.faces.push_back(std::move(face));
    return id;
}

const Vertex* find_vertex(const Document& document, ElementId id)
{
    const auto vertex = std::ranges::find_if(document.vertices, [id](const Vertex& candidate) {
        return candidate.id == id;
    });
    return vertex == document.vertices.end() ? nullptr : &(*vertex);
}

Vertex* find_vertex(Document& document, ElementId id)
{
    const auto vertex = std::ranges::find_if(document.vertices, [id](const Vertex& candidate) {
        return candidate.id == id;
    });
    return vertex == document.vertices.end() ? nullptr : &(*vertex);
}

const Face* find_face(const Document& document, ElementId id)
{
    const auto face = std::ranges::find_if(document.faces, [id](const Face& candidate) {
        return candidate.id == id;
    });
    return face == document.faces.end() ? nullptr : &(*face);
}

Face* find_face(Document& document, ElementId id)
{
    const auto face = std::ranges::find_if(document.faces, [id](const Face& candidate) {
        return candidate.id == id;
    });
    return face == document.faces.end() ? nullptr : &(*face);
}

std::vector<Edge> document_edges(const Document& document)
{
    std::set<std::pair<ElementId, ElementId>> unique_edges;
    for (const Face& face : document.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }

        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (edge.a != edge.b && edge.a != kInvalidElementId &&
                edge.b != kInvalidElementId) {
              unique_edges.emplace(edge.a, edge.b);
            }
        }
    }

    std::vector<Edge> edges;
    edges.reserve(unique_edges.size());
    for (const auto& [a, b] : unique_edges) {
        edges.push_back({ a, b });
    }
    return edges;
}

} // namespace quader_poly
