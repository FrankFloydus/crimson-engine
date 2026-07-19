////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

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
    // Authored IDs are append-only and therefore normally match their dense
    // storage slot. Keep the linear fallback for documents whose storage was
    // compacted or imported in a different order, but make the overwhelmingly
    // common modeling/compilation lookup O(1). This mirrors the indexed lookup
    // tables used by mature editable-mesh implementations without changing the
    // portable Document layout.
    if (id != kInvalidElementId) {
        const std::size_t hinted_index = static_cast<std::size_t>(id - 1U);
        if (hinted_index < document.vertices.size() &&
            document.vertices[hinted_index].id == id) {
          return &document.vertices[hinted_index];
        }
    }
    const auto vertex = std::ranges::find_if(document.vertices, [id](const Vertex& candidate) {
        return candidate.id == id;
    });
    return vertex == document.vertices.end() ? nullptr : &(*vertex);
}

Vertex* find_vertex(Document& document, ElementId id)
{
    if (id != kInvalidElementId) {
        const std::size_t hinted_index = static_cast<std::size_t>(id - 1U);
        if (hinted_index < document.vertices.size() &&
            document.vertices[hinted_index].id == id) {
          return &document.vertices[hinted_index];
        }
    }
    const auto vertex = std::ranges::find_if(document.vertices, [id](const Vertex& candidate) {
        return candidate.id == id;
    });
    return vertex == document.vertices.end() ? nullptr : &(*vertex);
}

const Face* find_face(const Document& document, ElementId id)
{
    if (id != kInvalidElementId) {
        const std::size_t hinted_index = static_cast<std::size_t>(id - 1U);
        if (hinted_index < document.faces.size() &&
            document.faces[hinted_index].id == id) {
          return &document.faces[hinted_index];
        }
    }
    const auto face = std::ranges::find_if(document.faces, [id](const Face& candidate) {
        return candidate.id == id;
    });
    return face == document.faces.end() ? nullptr : &(*face);
}

Face* find_face(Document& document, ElementId id)
{
    if (id != kInvalidElementId) {
        const std::size_t hinted_index = static_cast<std::size_t>(id - 1U);
        if (hinted_index < document.faces.size() &&
            document.faces[hinted_index].id == id) {
          return &document.faces[hinted_index];
        }
    }
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
